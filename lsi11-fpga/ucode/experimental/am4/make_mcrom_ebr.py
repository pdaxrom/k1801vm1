#!/usr/bin/env python3
"""Generate the original AM4 56x1024 MicROM as seven MachXO2 EBRs.

Diamond's generic Verilog ROM inference maps this unusually wide control
store into LUTs on the LCMXO2-1200HC.  Seven explicit 1024x9 DP8KC blocks
use the complete seven-EBR budget instead.  Each INITVAL holds 32 locations:
two adjacent 9-bit words form an 18-bit value in each 20-bit physical slot.

The generated Verilog is a build product.  ``mc.rom`` remains the source.
"""

import argparse
import re
from pathlib import Path


WORDS = 1024
WORD_BITS = 56
EBR_WIDTH = 9
EBR_COUNT = 7
INITVAL_COUNT = 32
WORDS_PER_INITVAL = 32
BOOT_FRAGMENT_BITS = 7
BOOT_FRAGMENTS_PER_WORD = 3
SERVICE_FRAGMENT_BASE = 640
SERVICE_FRAGMENT_STRIDE = 128
BOOT_ADDRESS_BYTES = (SERVICE_FRAGMENT_BASE // BOOT_FRAGMENTS_PER_WORD) * 2
SERVICE_MAIN_WORDS = SERVICE_FRAGMENT_STRIDE
SERVICE_EXTENSION_WORDS = SERVICE_FRAGMENT_STRIDE // 4
SERVICE_ADDRESS_BYTES = (SERVICE_MAIN_WORDS + SERVICE_EXTENSION_WORDS) * 2


def read_image(path):
    values = []
    for line_number, raw_line in enumerate(path.read_text().splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        if not re.fullmatch(r"[0-9A-Fa-f]{1,14}", line):
            raise ValueError(f"{path}:{line_number}: expected one 56-bit hexadecimal word")
        values.append(int(line, 16))
    if len(values) != WORDS:
        raise ValueError(f"{path}: expected {WORDS} words, found {len(values)}")
    if any(value >= 1 << WORD_BITS for value in values):
        raise ValueError(f"{path}: a word exceeds {WORD_BITS} bits")
    return values


def lane_initvals(image, lane):
    """Return the 32 physical INITVAL integers for one 9-bit EBR lane."""
    shift = lane * EBR_WIDTH
    mask = (1 << EBR_WIDTH) - 1
    result = []
    for row in range(INITVAL_COUNT):
        start = row * WORDS_PER_INITVAL
        packed = 0
        for pair in range(WORDS_PER_INITVAL // 2):
            low = (image[start + pair * 2] >> shift) & mask
            high = (image[start + pair * 2 + 1] >> shift) & mask
            packed |= (low | (high << EBR_WIDTH)) << (pair * 20)
        result.append(packed)
    return result


def encode_fragments(data):
    """Encode little-endian PDP-11 words as three seven-bit fragments."""
    if len(data) & 1:
        data += b"\0"
    fragments = []
    for offset in range(0, len(data), 2):
        word = data[offset] | (data[offset + 1] << 8)
        fragments += [word & 0x7f, (word >> 7) & 0x7f, (word >> 14) & 0x03]
    return fragments


def boot_fragments(data):
    """Pack the reset bootstrap below the fixed private-service partition."""
    if len(data) > BOOT_ADDRESS_BYTES:
        raise ValueError(
            f"boot image is {len(data)} bytes; AM4 boot partition is "
            f"{BOOT_ADDRESS_BYTES} bytes"
        )
    fragments = encode_fragments(data)
    return fragments + [0] * (WORDS - len(fragments))


def firmware_fragments(boot_data, service_data=b""):
    """Pack reset and private-service images into one spare-bit store."""
    image = boot_fragments(boot_data)
    if len(service_data) > SERVICE_ADDRESS_BYTES:
        raise ValueError(
            f"service image is {len(service_data)} bytes; AM4 service partition is "
            f"{SERVICE_ADDRESS_BYTES} bytes"
        )
    if len(service_data) & 1:
        service_data += b"\0"
    service_words = [service_data[offset] | (service_data[offset + 1] << 8)
                     for offset in range(0, len(service_data), 2)]
    for word_index, word in enumerate(service_words[:SERVICE_MAIN_WORDS]):
        image[SERVICE_FRAGMENT_BASE + word_index] = word & 0x7f
        image[SERVICE_FRAGMENT_BASE + SERVICE_FRAGMENT_STRIDE +
              word_index] = (word >> 7) & 0x7f
        image[SERVICE_FRAGMENT_BASE + 2 * SERVICE_FRAGMENT_STRIDE +
              word_index] = (word >> 14) & 0x03

    # The third service plane uses only two of its seven bits.  Three banks of
    # its otherwise-unused five-bit fields hold bits 14:0 of each additional
    # PDP-11 word.  Bit 15 is tagged on the third fragment for the behavioral
    # model and emitted as a small constant function by the EBR generator.
    # This keeps extension fetches at the same three cycles as ordinary words.
    for extension_index, word in enumerate(service_words[SERVICE_MAIN_WORDS:]):
        for chunk in range(3):
            address = SERVICE_FRAGMENT_BASE + 2 * SERVICE_FRAGMENT_STRIDE + \
                chunk * SERVICE_EXTENSION_WORDS + extension_index
            image[address] |= ((word >> (chunk * 5)) & 0x1f) << 2
            if chunk == 2 and word & 0x8000:
                image[address] |= 0x80
    return image


def embed_boot(image, fragments):
    """Place boot fragments in physical MicROM bits 62:56 without touching AM4."""
    if len(image) != WORDS or len(fragments) != WORDS:
        raise ValueError("MicROM and boot fragment images must contain 1024 words")
    return [word | ((fragment & 0x7f) << WORD_BITS)
            for word, fragment in zip(image, fragments)]


def generate(image, module_name="am4_mcrom", extension_high_indices=()):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", module_name):
        raise ValueError("invalid Verilog module name")

    lines = [
        "// Generated by ucode/experimental/am4/make_mcrom_ebr.py; do not edit or commit.",
        "`timescale 1ns/1ps",
        f"module {module_name} #(parameter MICROM_FILE = \"unused\", parameter BOOTROM_FILE = \"unused\") (",
        "    input wire clk,",
        "    input wire ena,",
        "    input wire [9:0] addr,",
        "    output wire [55:0] data,",
        "    input wire boot_ena,",
        "    input wire [9:0] boot_addr,",
        "    output wire [7:0] boot_data",
        ");",
        "    // Keep the Lattice timing model deterministic in vendor simulation.",
        "    // synthesis translate_off",
        "    GSR GSR_INST (.GSR(1'b1));",
        "    PUR PUR_INST (.PUR(1'b1));",
        "    // synthesis translate_on",
    ]

    for lane in range(EBR_COUNT):
        lo = lane * EBR_WIDTH
        hi = min(lo + EBR_WIDTH - 1, WORD_BITS - 1)
        lines += [
            f"    wire [8:0] lane_{lane};",
            "    DP8KC #(",
            "        .DATA_WIDTH_A(9), .DATA_WIDTH_B(9),",
            '        .REGMODE_A("NOREG"), .REGMODE_B("NOREG"),',
            '        .CSDECODE_A("0b000"), .CSDECODE_B("0b000"),' if lane == 6 else
            '        .CSDECODE_A("0b000"), .CSDECODE_B("0b111"),',
            '        .WRITEMODE_A("NORMAL"), .WRITEMODE_B("NORMAL"),',
            '        .GSR("DISABLED"), .RESETMODE("SYNC"),',
            '        .ASYNC_RESET_RELEASE("SYNC"), .INIT_DATA("STATIC"),',
        ]
        initvals = lane_initvals(image, lane)
        for row, value in enumerate(initvals):
            comma = "," if row != INITVAL_COUNT - 1 else ""
            lines.append(f'        .INITVAL_{row:02X}("0x{value:080X}"){comma}')
        lines += [
            f"    ) microm_lane_{lane} (",
            "        .DIA8(1'b0), .DIA7(1'b0), .DIA6(1'b0), .DIA5(1'b0),",
            "        .DIA4(1'b0), .DIA3(1'b0), .DIA2(1'b0), .DIA1(1'b0), .DIA0(1'b0),",
            "        .ADA12(addr[9]), .ADA11(addr[8]), .ADA10(addr[7]), .ADA9(addr[6]),",
            "        .ADA8(addr[5]), .ADA7(addr[4]), .ADA6(addr[3]), .ADA5(addr[2]),",
            "        .ADA4(addr[1]), .ADA3(addr[0]), .ADA2(1'b0), .ADA1(1'b0), .ADA0(1'b1),",
            "        .CEA(ena), .OCEA(ena), .CLKA(clk), .WEA(1'b0),",
            "        .CSA2(1'b0), .CSA1(1'b0), .CSA0(1'b0), .RSTA(1'b0),",
            "        .DIB8(1'b0), .DIB7(1'b0), .DIB6(1'b0), .DIB5(1'b0),",
            "        .DIB4(1'b0), .DIB3(1'b0), .DIB2(1'b0), .DIB1(1'b0), .DIB0(1'b0),",
            "        .ADB12(boot_addr[9]), .ADB11(boot_addr[8]), .ADB10(boot_addr[7]), .ADB9(boot_addr[6])," if lane == 6 else
            "        .ADB12(1'b0), .ADB11(1'b0), .ADB10(1'b0), .ADB9(1'b0),",
            "        .ADB8(boot_addr[5]), .ADB7(boot_addr[4]), .ADB6(boot_addr[3]), .ADB5(boot_addr[2])," if lane == 6 else
            "        .ADB8(1'b0), .ADB7(1'b0), .ADB6(1'b0), .ADB5(1'b0),",
            "        .ADB4(boot_addr[1]), .ADB3(boot_addr[0]), .ADB2(1'b0), .ADB1(1'b0), .ADB0(1'b1)," if lane == 6 else
            "        .ADB4(1'b0), .ADB3(1'b0), .ADB2(1'b0), .ADB1(1'b0), .ADB0(1'b0),",
            "        .CEB(boot_ena), .OCEB(boot_ena), .CLKB(clk), .WEB(1'b0)," if lane == 6 else
            "        .CEB(1'b1), .OCEB(1'b1), .CLKB(1'b0), .WEB(1'b0),",
            "        .CSB2(1'b0), .CSB1(1'b0), .CSB0(1'b0), .RSTB(1'b0),",
            f"        .DOA8(lane_{lane}[8]), .DOA7(lane_{lane}[7]), .DOA6(lane_{lane}[6]),",
            f"        .DOA5(lane_{lane}[5]), .DOA4(lane_{lane}[4]), .DOA3(lane_{lane}[3]),",
            f"        .DOA2(lane_{lane}[2]), .DOA1(lane_{lane}[1]), .DOA0(lane_{lane}[0]),",
            "        .DOB8(boot_data[6]), .DOB7(boot_data[5]), .DOB6(boot_data[4]), .DOB5(boot_data[3]), .DOB4(boot_data[2])," if lane == 6 else
            "        .DOB8(), .DOB7(), .DOB6(), .DOB5(), .DOB4(),",
            "        .DOB3(boot_data[1]), .DOB2(boot_data[0]), .DOB1(), .DOB0()" if lane == 6 else
            "        .DOB3(), .DOB2(), .DOB1(), .DOB0()",
            "    );",
            f"    assign data[{hi}:{lo}] = lane_{lane}[{hi - lo}:0];",
        ]
    high_terms = [f"(boot_addr[4:0] == 5'd{index})"
                  for index in extension_high_indices]
    high_expression = " || ".join(high_terms)
    lines.append("    assign boot_data[7] = " +
                 ("(boot_addr[9:5] == 5'b11110) && (" +
                  high_expression + ")" if high_terms else "1'b0") + ";")
    lines += ["endmodule", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--module-name", default="am4_mcrom")
    parser.add_argument("--boot-bin", type=Path)
    parser.add_argument("--service-bin", type=Path)
    parser.add_argument("--boot-rom-output", type=Path)
    parser.add_argument("--boot-rom-only", action="store_true")
    args = parser.parse_args()
    try:
        image = read_image(args.rom)
        boot_data = args.boot_bin.read_bytes() if args.boot_bin else b""
        service_data = args.service_bin.read_bytes() if args.service_bin else b""
        fragments = firmware_fragments(boot_data, service_data)
        if args.boot_rom_output:
            args.boot_rom_output.write_text("".join(f"{value:02x}\n" for value in fragments))
        if not args.boot_rom_only:
            high_indices = [
                index for index in range(SERVICE_EXTENSION_WORDS)
                if fragments[SERVICE_FRAGMENT_BASE +
                             2 * SERVICE_FRAGMENT_STRIDE +
                             2 * SERVICE_EXTENSION_WORDS + index] & 0x80
            ]
            print(generate(embed_boot(image, fragments), args.module_name,
                           high_indices), end="")
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
