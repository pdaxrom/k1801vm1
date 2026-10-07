#!/usr/bin/env python3
"""Prepare a narrowly matched PCB800099-V.9 NTSC capture-table patch.

Only supports the exact factory image read from the operator's display.
The two full copies and an independent controller-CRC report are required.
This tool writes local artifacts only; it never opens USB or programs flash.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from rtd266x_flash_read import crc8

FACTORY_SHA256 = 'a6bf61ec904d753d99f01ba1eaa8e75f103c98d5881f487ffac229de47f483f8'
SIZE = 512*1024
TABLE_OFFSET = 0x3fbf
SECTOR_OFFSET = 0x3000
SECTOR_SIZE = 4096
FACTORY_TABLE = struct.pack('>10H', 864, 155, 672, 31, 274, 858, 141, 696, 26, 232)


def prepare(first, second, verification):
    if len(first) != SIZE or first != second:
        raise ValueError('Two identical full 512 KiB backups are required')
    factory_hash = hashlib.sha256(first).hexdigest()
    if factory_hash != FACTORY_SHA256:
        raise ValueError('Unsupported factory firmware fingerprint')
    if not verification.get('passed') or verification.get('sha256') != factory_hash:
        raise ValueError('Independent hardware verification does not match this backup')
    if verification.get('hardware_crc8') != crc8(first):
        raise ValueError('Flash-controller CRC does not match the full backup')
    if first.find(FACTORY_TABLE) != TABLE_OFFSET or first.count(FACTORY_TABLE) != 1:
        raise ValueError('Expected unique PAL/NTSC table is absent')
    patched = bytearray(first)
    # Preserve PAL and NTSC horizontal geometry. These two words supply
    # stModeInfo.IVStartPos/IVHeight to capture/scaling/display setup.
    struct.pack_into('>HH', patched, TABLE_OFFSET+16, 18, 240)
    differences = [{'offset': at, 'before': old, 'after': new}
                   for at, (old, new) in enumerate(zip(first, patched)) if old != new]
    if differences != [{'offset': 0x3fd0, 'before': 26, 'after': 18},
                       {'offset': 0x3fd2, 'before': 232, 'after': 240}]:
        raise ValueError('Patch changed unexpected bytes')
    original_sector = first[SECTOR_OFFSET:SECTOR_OFFSET+SECTOR_SIZE]
    patched_sector = bytes(patched[SECTOR_OFFSET:SECTOR_OFFSET+SECTOR_SIZE])
    report = {'board': 'PCB800099-V.9', 'jedec': 'ef3013', 'size': SIZE,
              'factory_sha256': factory_hash, 'patched_sha256': hashlib.sha256(patched).hexdigest(),
              'factory_crc8': crc8(first), 'patched_crc8': crc8(patched),
              'sector_offset': SECTOR_OFFSET, 'sector_size': SECTOR_SIZE,
              'factory_sector_sha256': hashlib.sha256(original_sector).hexdigest(),
              'patched_sector_sha256': hashlib.sha256(patched_sector).hexdigest(),
              'table_offset': TABLE_OFFSET,
              'table_before': list(struct.unpack('>10H', FACTORY_TABLE)),
              'table_after': list(struct.unpack_from('>10H', patched, TABLE_OFFSET)),
              'differences': differences,
              'loader_reference_offset': 0x8190,
              'loader_evidence_hex': first[0x8190:0x81f1].hex(),
              'loader_interpretation': '5 big-endian words, field index * 10; capture/scaler/display use IVStartPos/IVHeight',
              'fpga_modified': False, 'flash_programmed': False,
              'physical_picture_verified': False}
    return bytes(patched), original_sector, patched_sector, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--factory-first', required=True, type=Path)
    parser.add_argument('--factory-second', required=True, type=Path)
    parser.add_argument('--verification', required=True, type=Path)
    parser.add_argument('--directory', required=True, type=Path, help='New output directory')
    args = parser.parse_args()
    image, old, new, report = prepare(args.factory_first.read_bytes(), args.factory_second.read_bytes(),
                                     json.loads(args.verification.read_text()))
    args.directory.mkdir()
    (args.directory/'ntsc-240.bin').write_bytes(image)
    (args.directory/'factory-sector-3000.bin').write_bytes(old)
    (args.directory/'ntsc-240-sector-3000.bin').write_bytes(new)
    (args.directory/'patch.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
