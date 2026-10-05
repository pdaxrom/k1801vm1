#!/usr/bin/env python3
"""BK native bootstrap FAT16 image, adapted from pyldin601-fpga/tools/make_sd.py.
Only regular new output files are created; never overwrite or open a device.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import zlib

from models import settings

ROOT = Path(__file__).resolve().parents[1]
SECTOR = 512
ALIGN = 2048
BOOT_SECTORS = 32768
MAGIC = b'BK0010\0\0'


def bundle(monitor, basic):
    if len(monitor) != 8192 or len(basic) != 24576:
        raise ValueError('Expected 8 KiB MONIT10 and 24 KiB BASIC10')
    payload = monitor + basic
    header = bytearray(512)
    header[:8] = MAGIC
    struct.pack_into('<HHIIHHI', header, 8, 1, 512, len(payload), 0x8000, 0x8000, 0, zlib.crc32(payload))
    struct.pack_into('<I', header, 28, zlib.crc32(header))
    return bytes(header) + payload


def validate_bundle(data):
    if len(data) != 33280 or data[:8] != MAGIC:
        raise ValueError('Invalid BK ROM size or magic')
    version, size, length, base, entry, reserved, crc = struct.unpack_from('<HHIIHHI', data, 8)
    if (version, size, length, base, entry, reserved) != (1, 512, 32768, 32768, 32768, 0):
        raise ValueError('Invalid ROM header fields')
    header = bytearray(data[:512])
    expected = struct.unpack_from('<I', header, 28)[0]
    header[28:32] = b'\0' * 4
    if zlib.crc32(header) != expected or zlib.crc32(data[512:]) != crc:
        raise ValueError('ROM CRC mismatch')
    return data[512:]


def fat16(files):
    total, reserved, copies, root_entries, spc = BOOT_SECTORS, 1, 2, 512, 1
    spf = 128
    root_sectors = root_entries // 16
    data_start = reserved + copies * spf + root_sectors
    clusters = (total - data_start) // spc
    assert 4085 <= clusters < 65525 and (clusters + 2) * 2 <= spf * SECTOR
    volume = bytearray(total * SECTOR)
    volume[:11] = b'\xeb\x3c\x90BK10BOOT'
    struct.pack_into('<HBHBHHBHHHII', volume, 11, SECTOR, spc, reserved,
                     copies, root_entries, total, 0xf8, spf, 32, 2, ALIGN, 0)
    volume[38] = 0x29
    struct.pack_into('<I', volume, 39, 0x1001)
    volume[43:54] = b'BK0010 ROM '
    volume[54:62] = b'FAT16   '
    volume[510:512] = b'\x55\xaa'
    table = bytearray(spf * SECTOR)
    struct.pack_into('<HH', table, 0, 0xfff8, 0xffff)
    cluster = 2
    root_start = (reserved + copies * spf) * SECTOR
    for index, (name, data) in enumerate(files.items()):
        base, ext = name.split('.')
        if len(base) > 8 or len(ext) > 3 or name != name.upper():
            raise ValueError(f'not an uppercase 8.3 name: {name}')
        count = (len(data) + SECTOR - 1) // SECTOR
        if not count or cluster + count > clusters + 2 or index >= root_entries:
            raise ValueError('boot partition full')
        entry = root_start + index * 32
        volume[entry:entry+11] = (base.ljust(8)+ext.ljust(3)).encode('ascii')
        volume[entry+11] = 0x20
        struct.pack_into('<H', volume, entry+26, cluster)
        struct.pack_into('<I', volume, entry+28, len(data))
        offset = (data_start + cluster - 2) * SECTOR
        volume[offset:offset+len(data)] = data
        for c in range(cluster, cluster + count):
            struct.pack_into('<H', table, c*2, 0xffff if c == cluster+count-1 else c+1)
        cluster += count
    for i in range(copies):
        off = (reserved + i * spf) * SECTOR
        volume[off:off+len(table)] = table
    return bytes(volume)



def build(loader, monitor, basic):
    if not 1 <= len(loader) <= 4096:
        raise ValueError('LOADERBK.BIN must fit 020000..027777')
    rom = bundle(monitor, basic)
    image = bytearray((ALIGN + BOOT_SECTORS) * SECTOR)
    image[510:512] = b'\x55\xaa'
    struct.pack_into('<B3sB3sII', image, 446, 0, b'\xfe\xff\xff', 0x06,
                     b'\xfe\xff\xff', ALIGN, BOOT_SECTORS)
    image[ALIGN*SECTOR:] = fat16({'LOADERBK.BIN': loader, 'BK0010.ROM': rom, 'BK.SET': settings()})
    return bytes(image)


def save_new(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation makes stale images and devices an error.
    with path.open('xb') as output:
        if not stat.S_ISREG(os.fstat(output.fileno()).st_mode):
            raise ValueError('Output must be a regular file')
        output.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--loader', type=Path, default=ROOT/'build/LOADERBK.BIN')
    parser.add_argument('--rom-dir', type=Path, default=ROOT/'roms')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    monitor = (args.rom_dir/'MONIT10.ROM').read_bytes()
    basic = (args.rom_dir/'BASIC10.ROM').read_bytes()
    image = build(args.loader.read_bytes(), monitor, basic)
    save_new(args.out, image)
    print(json.dumps({'file': str(args.out), 'bytes': len(image), 'sha256': hashlib.sha256(image).hexdigest()}))


if __name__ == '__main__':
    main()
