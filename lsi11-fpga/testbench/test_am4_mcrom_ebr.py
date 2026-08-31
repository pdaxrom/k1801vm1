import importlib.util
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "ucode/experimental/am4/make_mcrom_ebr.py"
ROM = ROOT / "ucode/experimental/am4/mc.rom"
MICROM_SOURCE = ROOT / "ucode/experimental/am4/mc.asm"
MICROM_ASSEMBLER = ROOT / "ucode/experimental/am4/assemble_mcrom.py"

spec = importlib.util.spec_from_file_location("make_am4_mcrom_ebr", GENERATOR)
ebr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ebr)


class Am4McromEbrTest(unittest.TestCase):
    def test_recovered_source_assembles_to_committed_rom(self):
        with tempfile.TemporaryDirectory(prefix="am4-microm-test-") as temporary:
            output = Path(temporary) / "mc.rom"
            subprocess.run(
                [sys.executable, str(MICROM_ASSEMBLER),
                 str(MICROM_SOURCE), str(output)],
                check=True,
            )
            self.assertEqual(output.read_bytes(), ROM.read_bytes())

    def test_every_word_survives_physical_initval_packing(self):
        image = ebr.read_image(ROM)
        lanes = [ebr.lane_initvals(image, lane) for lane in range(ebr.EBR_COUNT)]
        recovered = []
        for address in range(ebr.WORDS):
            row = address // ebr.WORDS_PER_INITVAL
            offset = address % ebr.WORDS_PER_INITVAL
            pair = offset // 2
            half_shift = ebr.EBR_WIDTH if offset & 1 else 0
            word = 0
            for lane in range(ebr.EBR_COUNT):
                physical = lanes[lane][row] >> (pair * 20 + half_shift)
                word |= (physical & 0x1FF) << (lane * ebr.EBR_WIDTH)
            recovered.append(word & ((1 << ebr.WORD_BITS) - 1))
        self.assertEqual(recovered, image)

    def test_generated_module_is_exactly_seven_1024x9_ebrs(self):
        verilog = ebr.generate(ebr.read_image(ROM))
        self.assertEqual(verilog.count("    DP8KC #("), 7)
        self.assertEqual(verilog.count(".DATA_WIDTH_A(9)"), 7)
        self.assertEqual(len(re.findall(r"\.INITVAL_[0-9A-F]{2}\(", verilog)), 7 * 32)
        self.assertIn("assign data[55:54] = lane_6[1:0];", verilog)
        self.assertIn(".DOB8(boot_data[6])", verilog)

    def test_boot_fragments_preserve_microm_and_round_trip_words(self):
        payload = bytes((index * 37 + 11) & 0xff for index in range(381))
        fragments = ebr.boot_fragments(payload)
        physical = ebr.embed_boot(ebr.read_image(ROM), fragments)
        self.assertEqual([word & ((1 << ebr.WORD_BITS) - 1) for word in physical],
                         ebr.read_image(ROM))
        recovered = bytearray()
        for index in range(0, len(payload) + 1, 2):
            base = (index // 2) * ebr.BOOT_FRAGMENTS_PER_WORD
            word = (fragments[base] | (fragments[base + 1] << 7) |
                    (fragments[base + 2] << 14))
            recovered += word.to_bytes(2, "little")
        self.assertEqual(bytes(recovered[:len(payload)]), payload)

    def test_boot_image_cannot_exceed_mapped_window(self):
        self.assertEqual(len(ebr.boot_fragments(bytes(ebr.BOOT_ADDRESS_BYTES))),
                         ebr.WORDS)
        with self.assertRaisesRegex(ValueError, "AM4 boot partition"):
            ebr.boot_fragments(bytes(ebr.BOOT_ADDRESS_BYTES + 1))

    def test_private_service_has_a_fixed_nonoverlapping_partition(self):
        boot = bytes((index * 3) & 0xff
                     for index in range(ebr.BOOT_ADDRESS_BYTES))
        service_words = [((index * 0x151 + 1) & 0x7fff)
                         for index in range(ebr.SERVICE_MAIN_WORDS +
                                            ebr.SERVICE_EXTENSION_WORDS)]
        for extension_index in range(ebr.SERVICE_EXTENSION_WORDS):
            shared_high = (service_words[64 + extension_index] >> 14) & 1
            service_words[ebr.SERVICE_MAIN_WORDS + extension_index] |= \
                shared_high << 15
        service = b"".join(word.to_bytes(2, "little")
                           for word in service_words)
        fragments = ebr.firmware_fragments(boot, service)
        self.assertEqual(fragments[:len(ebr.encode_fragments(boot))],
                         ebr.encode_fragments(boot))
        recovered = bytearray()
        for word_index in range(len(service) // 2):
            if word_index < ebr.SERVICE_MAIN_WORDS:
                word = sum(
                    fragments[ebr.SERVICE_FRAGMENT_BASE +
                              plane * ebr.SERVICE_FRAGMENT_STRIDE + word_index]
                    << (plane * 7)
                    for plane in range(ebr.BOOT_FRAGMENTS_PER_WORD)
                )
            else:
                extension_index = word_index - ebr.SERVICE_MAIN_WORDS
                word = sum(
                    ((fragments[ebr.SERVICE_FRAGMENT_BASE +
                                2 * ebr.SERVICE_FRAGMENT_STRIDE +
                                chunk * ebr.SERVICE_EXTENSION_WORDS +
                                extension_index] >> 2) & 0x1f)
                    << (chunk * 5)
                    for chunk in range(3)
                )
                high_fragment = fragments[
                    ebr.SERVICE_FRAGMENT_BASE +
                    2 * ebr.SERVICE_FRAGMENT_STRIDE +
                    2 * ebr.SERVICE_EXTENSION_WORDS + extension_index
                ]
                word |= (high_fragment & 0x80) << 8
            recovered += (word & 0xffff).to_bytes(2, "little")
        self.assertEqual(bytes(recovered), service)
        with self.assertRaisesRegex(ValueError, "AM4 service partition"):
            ebr.firmware_fragments(b"", bytes(ebr.SERVICE_ADDRESS_BYTES + 1))


if __name__ == "__main__":
    unittest.main()
