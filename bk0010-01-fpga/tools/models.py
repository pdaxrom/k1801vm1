#!/usr/bin/env python3
"""Machine ROM contracts, BIOS settings and bounded raw SD disk partitions."""
import argparse
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = json.loads((ROOT / 'config/machines.json').read_text())
MODELS = CONTRACT['models']
FREQUENCIES = (3, 4, 6, 8, 12, 16, 24)
SETTINGS_MAGIC = b'BKBIOS\0\0'
BUNDLE_MAGIC = b'BKROM2\0\0'
DISK_SECTORS = 1600


def crc16(data, initial=0):
    crc = initial
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def validate_settings(model, frequency, color, drives, readonly):
    if model not in range(5) or frequency not in FREQUENCIES or color not in (0, 1):
        raise ValueError('Invalid model, frequency or video mode')
    if len(drives) != 4 or readonly & ~15:
        raise ValueError('Invalid disk configuration')
    used = set()
    for index, partition in enumerate(drives):
        if partition not in (0, 2, 3, 4, 5, 6):
            raise ValueError('Disk partition must be 0 or 2..6; boot partition is protected')
        if partition:
            if not MODELS[model]['drive_mask'] & (1 << index) or partition in used:
                raise ValueError('Unavailable drive or duplicate partition')
            used.add(partition)


def settings(model=2, frequency=None, color=0, drives=None, readonly=0):
    if model not in range(5):
        raise ValueError('Invalid model')
    frequency = MODELS[model]['default_mhz'] if frequency is None else frequency
    drives = tuple((2, 3, 0, 0) if MODELS[model]['drive_mask'] else (0, 0, 0, 0)) if drives is None else tuple(drives)
    validate_settings(model, frequency, color, drives, readonly)
    data = bytearray(SETTINGS_MAGIC)
    data.extend((1, model, FREQUENCIES.index(frequency) | (color << 3),
                 drives[0] | (drives[1] << 4), drives[2] | (drives[3] << 4), readonly))
    data.extend(struct.pack('<H', crc16(data)))
    return bytes(data)


def parse_settings(data):
    if len(data) != 16 or data[:8] != SETTINGS_MAGIC or data[8] != 1 or crc16(data[:14]) != int.from_bytes(data[14:], 'little'):
        raise ValueError('Invalid BK.SET size, magic, version or CRC')
    if data[10] & 0xf0 or (data[10] & 7) >= len(FREQUENCIES):
        raise ValueError('Invalid frequency/video byte')
    result = dict(model=data[9], frequency=FREQUENCIES[data[10] & 7], color=(data[10] >> 3) & 1,
                  drives=(data[11] & 15, data[11] >> 4, data[12] & 15, data[12] >> 4), readonly=data[13])
    validate_settings(**result)
    return result


def bundle(model, rom_dir=ROOT / 'roms'):
    description = MODELS[model]
    header, payload = bytearray(512), bytearray()
    header[:8] = BUNDLE_MAGIC
    struct.pack_into('<HHHHII', header, 8, 2, 512, model, len(description['segments']), description['entry'], 0)
    for index, (name, physical, size) in enumerate(description['segments']):
        data = (Path(rom_dir) / name).read_bytes()
        if len(data) != size:
            raise ValueError(f'{name}: expected {size} bytes, got {len(data)}')
        if model == 0 and name == 'DISK_A16M_v2.41.rom' and int.from_bytes(data[4094:4096], 'little') & 0xff00 != 0o175000:
            raise ValueError('A16M ROM has no verified model signature at 0167776')
        struct.pack_into('<IIII', header, 32 + index * 16, physical, size, len(payload), zlib.crc32(data))
        payload.extend(data)
    struct.pack_into('<II', header, 20, len(payload), zlib.crc32(payload))
    struct.pack_into('<I', header, 28, zlib.crc32(header))
    return bytes(header + payload)


def parse_bundle(data, model=None):
    if len(data) < 512 or data[:8] != BUNDLE_MAGIC:
        raise ValueError('Invalid ROM bundle magic/size')
    version, size, ident, count, entry, length, payload_crc, header_crc = struct.unpack_from('<HHHHIIII', data, 8)
    if version != 2 or size != 512 or ident not in range(5) or model is not None and model != ident:
        raise ValueError('Invalid ROM bundle version/model')
    expected = MODELS[ident]
    if count != len(expected['segments']) or entry != expected['entry'] or length != len(data) - 512:
        raise ValueError('Invalid ROM bundle entry/segment count/length')
    header = bytearray(data[:512])
    header[28:32] = bytes(4)
    if zlib.crc32(header) != header_crc or zlib.crc32(data[512:]) != payload_crc:
        raise ValueError('ROM bundle CRC mismatch')
    position = 0
    result = []
    for index, (name, physical, size) in enumerate(expected['segments']):
        address, length, offset, crc = struct.unpack_from('<IIII', data, 32 + index * 16)
        if (address, length, offset) != (physical, size, position):
            raise ValueError('ROM segment map differs from machine contract')
        segment = data[512 + offset:512 + offset + length]
        if len(segment) != length or zlib.crc32(segment) != crc:
            raise ValueError('ROM segment CRC mismatch')
        result.append((address, segment))
        position += length
    if position != len(data) - 512 or any(data[32 + count * 16:512]):
        raise ValueError('Unexpected ROM bundle trailing data')
    return ident, result


def partitions(mbr, card_sectors, read_sector=None):
    if len(mbr) != 512 or mbr[510:] != b'\x55\xaa' or not 0 < card_sectors <= 0xffffffff:
        raise ValueError('Invalid MBR or SD capacity')
    result = []
    for index in range(4):
        entry = mbr[446 + index * 16:462 + index * 16]
        kind = entry[4]
        start, length = struct.unpack_from('<II', entry, 8)
        if not kind and not start and not length:
            result.append(None)
            continue
        if entry[0] not in (0, 0x80) or not kind or not start or not length or start + length > card_sectors:
            raise ValueError('Invalid partition bounds')
        if kind == 0xee or kind in (5, 0x0f, 0x85) and index != 3:
            raise ValueError('Only partition 4 may contain an extended chain')
        if any(p and start < p['start'] + p['length'] and p['start'] < start + length for p in result):
            raise ValueError('Overlapping SD partitions')
        result.append(dict(index=index + 1, type=kind, start=start, length=length))
    if not result[0] or result[0]['type'] not in (6, 0x0e):
        raise ValueError('Partition 1 must contain FAT16 boot files')
    extended = result[3]
    if extended and extended['type'] in (5, 0x0f, 0x85):
        if read_sector is None:
            raise ValueError('An EBR reader is required for an extended partition')
        base, end = extended['start'], extended['start']+extended['length']
        current = base
        for index in range(2):
            sector = read_sector(current)
            if len(sector) != 512 or sector[510:] != b'\x55\xaa' or any(sector[478:510]):
                raise ValueError('Invalid EBR signature or extra entries')
            entry = sector[446:462]
            kind, relative, length = entry[4], *struct.unpack_from('<II',entry,8)
            start = current+relative
            if entry[0] not in (0,0x80) or not kind or kind in (5,0x0f,0x85,0xee) or not relative or not length or start+length > end:
                raise ValueError('Invalid logical disk extent')
            result.append(dict(index=index+5,type=kind,start=start,length=length))
            link = sector[462:478]
            kind, relative, length = link[4], *struct.unpack_from('<II',link,8)
            if not kind and not relative and not length:
                result.extend([None]*(1-index))
                break
            following = base+relative
            if index == 1 or link[0] or kind not in (5,0x0f,0x85) or not relative or not length or following < start+result[-1]['length'] or following+length > end:
                raise ValueError('Invalid, overlapping or excessive EBR chain')
            current = following
    return result


def disk_lba(descriptors, partition, track, side, sector):
    if partition not in (2, 3, 4, 5, 6) or not 0 <= track < 80 or side not in (0, 1) or not 1 <= sector <= 10:
        raise ValueError('Invalid disk partition/CHS')
    descriptor = descriptors[partition - 1]
    if not descriptor or descriptor['type'] in (5,0x0f,0x85,0xee) or descriptor['length'] < DISK_SECTORS:
        raise ValueError('Absent or short disk partition')
    lba = descriptor['start'] + (track * 2 + side) * 10 + sector - 1
    if lba >= descriptor['start'] + descriptor['length'] or lba > 0xffffffff:
        raise ValueError('Disk LBA outside partition')
    return lba


def generate_layout():
    values = CONTRACT['physical']
    c = ['/* Generated by tools/models.py from config/machines.json. */', '#ifndef BK_MODEL_LAYOUT_H', '#define BK_MODEL_LAYOUT_H']
    v = ['// Generated by tools/models.py from config/machines.json.']
    for name, value in values.items():
        c.append(f'#define BK_{name} 0x{value:x}u')
        v.append(f'`define BK_{name} 20\'h{value:05x}')
    c.extend(('#endif', ''))
    (ROOT.parent / 'bk0010-01/bk_model_layout.h').write_text('\n'.join(c))
    (ROOT / 'rtl/bk_model_layout.vh').write_text('\n'.join(v) + '\n')
    rom_c = ['/* Generated by tools/models.py from config/machines.json. */',
             '#ifndef BK_ROM_CONTRACT_H', '#define BK_ROM_CONTRACT_H', '#include <stdint.h>',
             'typedef struct {', '\tconst char *name;', '\tuint32_t physical;', '\tunsigned int size;', '} bk_rom_spec;',
             'static const unsigned int bk_rom_counts[5] = {' +
             ','.join(str(len(model['segments'])) for model in MODELS) + '};',
             'static const bk_rom_spec bk_rom_specs[5][6] = {']
    for model in MODELS:
        rom_c.append('\t{')
        for name, physical, size in model['segments']:
            rom_c.append(f'\t\t{{"{name}", 0x{physical:x}u, {size}}},')
        rom_c.append('\t},')
    rom_c.extend(('};', '#endif', ''))
    (ROOT.parent / 'bk0010-01/bk_rom_contract.h').write_text('\n'.join(rom_c))
    palettes = json.loads((ROOT / 'config/video.json').read_text())['palettes_rgb24']
    colors_c = ['/* Generated by tools/models.py from config/video.json. */', '#ifndef BK_PALETTES_H',
                '#define BK_PALETTES_H', '#include <stdint.h>', 'static const uint32_t bk_palettes[16][4] = {']
    colors_v = ['// Generated by tools/models.py from config/video.json.', 'function [7:0] bk_palette_rgb;',
                '    input [3:0] palette;', '    input [1:0] pixel;', '    begin', '        case ({palette,pixel})']
    for palette, colors in enumerate(palettes):
        colors_c.append('\t{' + ', '.join(f'0xff{color}u' for color in colors) + '},')
        for pixel, color in enumerate(colors):
            red, green, blue = (int(color[position:position + 2], 16) for position in (0, 2, 4))
            rgb332 = round(red * 7 / 255) << 5 | round(green * 7 / 255) << 2 | round(blue * 3 / 255)
            if pixel:
                colors_v.append(f"            6'h{palette * 4 + pixel:02x}: bk_palette_rgb = 8'h{rgb332:02x};")
    colors_c.extend(('};', '#endif', ''))
    colors_v.extend(("            default: bk_palette_rgb = 0;", '        endcase', '    end', 'endfunction', ''))
    (ROOT.parent / 'bk0010-01/bk_palettes.h').write_text('\n'.join(colors_c))
    (ROOT / 'rtl/bk_palettes.vh').write_text('\n'.join(colors_v))
    assembly = ['; Generated by tools/models.py from config/machines.json.', 'bios_contract_labels:',
                '        dw ' + ','.join(f'bios_contract{model}' for model in range(5))]
    for model, description in enumerate(MODELS):
        segments = description['segments']
        assembly.extend((f'bios_contract{model}:',
                         f'        dw {len(segments)},{sum(segment[2] for segment in segments)}.,0x{description["entry"]:04x}'))
        offset = 0
        for _, physical, size in segments:
            assembly.append(f'        dw 0x{physical & 0xffff:04x},{physical >> 16},{size}.,0,{offset}.,0')
            offset += size
    (ROOT / 'build').mkdir(exist_ok=True)
    (ROOT / 'build/model-contract.inc').write_text('\n'.join(assembly) + '\n')
    (ROOT / 'build/model-drive-masks.inc').write_text('; Generated from config/machines.json.\n        db '+
        ','.join(f'0x{model["drive_mask"]:02x}' for model in MODELS)+',0\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build')
    parser.add_argument('--rom-dir', type=Path, default=ROOT / 'roms')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    generate_layout()
    status = []
    for model in range(5):
        try:
            data = bundle(model, args.rom_dir)
            parse_bundle(data, model)
            path = args.out / f'BK{model:02}.ROM'
            path.write_bytes(data)
            status.append(dict(model=model, file=path.name, bytes=len(data), available=True))
        except FileNotFoundError as error:
            status.append(dict(model=model, available=False, reason=str(error)))
    (args.out / 'models.json').write_text(json.dumps(status, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(status, ensure_ascii=False))


if __name__ == '__main__':
    main()
