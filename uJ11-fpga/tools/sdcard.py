#!/usr/bin/env python3
"""Partition/import/export an uJ11 SD image (512-byte sectors, little endian)."""
import argparse
from dataclasses import dataclass, replace
import json
import os
from pathlib import Path
import stat
import struct
import zlib
import storage_menu

MAGIC = b'UJ11SD\0\0'
HEADER = struct.Struct('<8sHHHHIIIIII24s')
ENTRY = struct.Struct('<BBBBHBBIIQ8s')
MAX_PARTS = 14
RESERVED = 2048
BOOT, READONLY = 1, 2
MENU = 1
# Kind numbers match lsi11/demo/boot_menu.asm. Media IDs are specific to v1.
KINDS = {'rk': 1, 'rh': 2, 'xp': 3, 'rq': 4, 'rl': 5, 'tq': 6}
MEDIA = {'rk05': (1, 1, 4872), 'rk06': (2, 2, 27126),
         'rk07': (2, 3, 53790), 'rm05': (3, 4, 500384),
         'mscp': (4, 5, 0), 'rl01': (5, 6, 10240),
         'rl02': (5, 7, 20480), 'tk50': (6, 8, 0)}

@dataclass(frozen=True)
class Partition:
    kind: int
    controller: int
    unit: int
    media: int
    flags: int
    mode: int
    start: int
    blocks: int
    used: int
    label: str = ''

@dataclass(frozen=True)
class Label:
    blocks: int
    partitions: tuple = ()
    sequence: int = 0
    reserved: int = RESERVED
    features: int = 0

    def validate(self):
        if not 2 <= self.reserved < self.blocks <= 0xffffffff:
            raise ValueError('invalid card/reserved size')
        if self.features & ~MENU or (self.features & MENU and self.reserved < storage_menu.MIN_RESERVED):
            raise ValueError('unsupported features or insufficient space for menu')
        if len(self.partitions) > MAX_PARTS:
            raise ValueError('at most 14 partitions')
        if not 0 <= self.sequence <= 0xffffffff:
            raise ValueError('invalid sequence')
        keys, intervals, modes = set(), [], {}
        boots = 0
        for p in self.partitions:
            geometry = next((v for v in MEDIA.values() if v[1] == p.media), None)
            if geometry is None or geometry[0] != p.kind:
                raise ValueError('invalid controller/media combination')
            if p.controller != 0 or not 0 <= p.unit < (4 if p.kind in (4, 5) else 8):
                raise ValueError('v1 supports controller 0 and units 0..3 or 0..7')
            if p.mode not in ((0, 1) if p.kind == 2 else (0,)):
                raise ValueError('mode RH70 is valid only for RH')
            if p.kind in modes and modes[p.kind] != p.mode:
                raise ValueError('all units of a controller must use the same mode')
            modes[p.kind] = p.mode
            if p.flags & ~3:
                raise ValueError('unknown flags')
            if p.start < self.reserved or p.blocks <= 0 or p.start + p.blocks > self.blocks:
                raise ValueError('partition outside card data area')
            if geometry[2] and p.blocks != geometry[2]:
                raise ValueError('partition size does not match media geometry')
            if not 0 <= p.used <= p.blocks * 512 or (p.kind != 6 and p.used != p.blocks * 512):
                raise ValueError('invalid image byte count')
            if len(p.label.encode('ascii')) > 8 or '\0' in p.label or any(ord(c) < 32 or ord(c) > 126 for c in p.label):
                raise ValueError('label must be at most 8 printable ASCII characters')
            key = p.kind, p.controller, p.unit
            if key in keys:
                raise ValueError('duplicate controller/unit')
            keys.add(key)
            intervals.append((p.start, p.start + p.blocks))
            boots += bool(p.flags & BOOT)
        if boots > 1:
            raise ValueError('only one bootable partition')
        intervals.sort()
        if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
            raise ValueError('overlapping partitions')
        return self

    def pack(self):
        self.validate()
        data = bytearray(HEADER.pack(MAGIC, 1, 64, 32, len(self.partitions),
            self.sequence, 0, self.blocks, self.reserved, 1, self.features, bytes(24)))
        for p in self.partitions:
            data += ENTRY.pack(p.kind, p.controller, p.unit, p.media, p.flags,
                p.mode, 0, p.start, p.blocks, p.used, p.label.encode('ascii').ljust(8, b'\0'))
        data += bytes(512 - len(data))
        struct.pack_into('<I', data, 20, zlib.crc32(data))
        return bytes(data)

    @classmethod
    def unpack(cls, raw):
        if len(raw) != 512:
            raise ValueError('short SD label')
        magic, ver, hdr, ent, count, seq, crc, size, reserved, backup, flags, zeros = HEADER.unpack(raw[:64])
        if magic != MAGIC:
            raise ValueError('no uJ11 SD label')
        if (ver, hdr, ent, backup, zeros) != (1, 64, 32, 1, bytes(24)) or count > MAX_PARTS:
            raise ValueError('unsupported/malformed SD label')
        check = bytearray(raw)
        check[20:24] = bytes(4)
        if zlib.crc32(check) != crc:
            raise ValueError('SD label CRC32 mismatch')
        if any(raw[64 + count * 32:]):
            raise ValueError('nonzero unused entries')
        parts = []
        for i in range(count):
            k, c, u, m, f, mode, zero, start, blocks, used, name = ENTRY.unpack_from(raw, 64 + i * 32)
            if zero or (b'\0' in name and any(name[name.index(0):])):
                raise ValueError('nonzero entry reserved bytes')
            parts.append(Partition(k, c, u, m, f, mode, start, blocks, used, name.rstrip(b'\0').decode('ascii')))
        return cls(size, tuple(parts), seq, reserved, flags).validate()


def read_label(f):
    errors = []
    for lba in (0, 1):
        f.seek(lba * 512)
        try:
            label = Label.unpack(f.read(512))
            if stat.S_ISREG(os.fstat(f.fileno()).st_mode) and os.fstat(f.fileno()).st_size < label.blocks * 512:
                raise ValueError('label exceeds backing file')
            return label, lba
        except ValueError as e:
            errors.append(str(e))
    raise ValueError('; '.join(errors))


def publish(f, label):
    raw = label.pack()
    # Primary is authoritative. If a write tears, a CRC-valid backup is usable.
    for lba in (1, 0):
        f.seek(lba * 512)
        f.write(raw)
        f.flush()
        os.fsync(f.fileno())


def copy_bytes(src, dst, count):
    while count:
        b = src.read(min(count, 1024 * 1024))
        if not b:
            raise ValueError('short input image')
        dst.write(b)
        count -= len(b)


def add_partition(label, media, unit, blocks=None, start=None, boot=False, readonly=False, mode=0, name=''):
    kind, medium, fixed = MEDIA[media]
    blocks = fixed if blocks is None else blocks
    if not blocks:
        raise ValueError('--blocks is required for variable-size media')
    if start is None:
        start = label.reserved
        for p in sorted(label.partitions, key=lambda p: p.start):
            if start + blocks <= p.start:
                break
            start = max(start, ((p.start + p.blocks + 2047) // 2048) * 2048)
    p = Partition(kind, 0, unit, medium, (BOOT if boot else 0) | (READONLY if readonly else 0),
                  mode, start, blocks, 0 if kind == 6 else blocks * 512, name)
    return replace(label, partitions=label.partitions + (p,), sequence=(label.sequence + 1) & 0xffffffff).validate()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', action='store_true', help='explicitly allow a raw block/character device')
    parser.add_argument('card', type=Path)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('init', help='create a new sparse card image; raw devices retain their data area')
    p.add_argument('--blocks', type=int, required=True)
    sub.add_parser('list')
    sub.add_parser('verify')
    p = sub.add_parser('menu', help='install a checked second-stage menu image in reserved sectors')
    p.add_argument('image', type=Path)
    sub.add_parser('menu-off', help='disable menu and return to direct boot')
    sub.add_parser('boot-off', help='clear the default/autoboot flag on all partitions')
    p = sub.add_parser('add')
    p.add_argument('media', choices=MEDIA)
    p.add_argument('unit', type=int)
    p.add_argument('--blocks', type=int)
    p.add_argument('--start', type=int)
    p.add_argument('--boot', action='store_true')
    p.add_argument('--readonly', action='store_true')
    p.add_argument('--rh-mode', choices=('rh11', 'rh70'), default='rh11')
    p.add_argument('--label', default='')
    for cmd in ('import', 'export', 'boot'):
        p = sub.add_parser(cmd)
        p.add_argument('kind', choices=KINDS)
        p.add_argument('unit', type=int)
        if cmd != 'boot':
            p.add_argument('image', type=Path)
    args = parser.parse_args(argv)
    device = args.card.exists() and not args.card.is_file()
    if device and (not args.device or not (stat.S_ISBLK(args.card.stat().st_mode) or stat.S_ISCHR(args.card.stat().st_mode))):
        parser.error('raw device access requires --device and a block/character device')
    if args.cmd == 'init':
        label = Label(args.blocks).validate()
        with args.card.open('r+b' if device else 'x+b') as f:
            if device:
                f.seek((args.blocks - 1) * 512)
                if len(f.read(512)) != 512:
                    raise ValueError('requested card size is not readable')
            else:
                f.truncate(args.blocks * 512)
            publish(f, label)
        return
    with args.card.open('rb' if args.cmd in ('list', 'verify', 'export') else 'r+b') as f:
        label, source = read_label(f)
        if args.cmd in ('list', 'verify'):
            print(json.dumps({'blocks': label.blocks, 'label_lba': source, 'sequence': label.sequence,
                'features': label.features,
                'menu': storage_menu.read(f) if label.features & MENU else None,
                'partitions': [vars(p) for p in label.partitions]}, indent=2))
            if args.cmd == 'verify':
                f.seek(0)
                if f.read(512) != label.pack() or f.read(512) != label.pack():
                    raise ValueError('label copies differ; one copy may need recovery')
            return
        if args.cmd == 'menu':
            if args.image.stat().st_size > (storage_menu.MAX_BLOCKS + 1) * 512:
                raise ValueError('menu image exceeds reserved loader capacity')
            data = args.image.read_bytes()
            storage_menu.unpack(data)
            updated = replace(label, features=label.features | MENU,
                              sequence=(label.sequence + 1) & 0xffffffff).validate()
            # Disable an older menu before replacement; a failed copy must not
            # leave its flag claiming that a partial payload is usable.
            if label.features & MENU:
                publish(f, replace(label, features=label.features & ~MENU))
            f.seek(2 * 512);f.write(data);f.flush();os.fsync(f.fileno())
            label = updated
        elif args.cmd == 'boot-off':
            label = replace(label, partitions=tuple(replace(p, flags=p.flags & ~BOOT) for p in label.partitions),
                            sequence=(label.sequence + 1) & 0xffffffff)
        elif args.cmd == 'menu-off':
            label = replace(label, features=label.features & ~MENU,
                            sequence=(label.sequence + 1) & 0xffffffff)
        elif args.cmd == 'add':
            label = add_partition(label, args.media, args.unit, args.blocks, args.start,
                args.boot, args.readonly, int(args.rh_mode == 'rh70'), args.label)
        else:
            p = next((p for p in label.partitions if (p.kind, p.unit) == (KINDS[args.kind], args.unit)), None)
            if p is None:
                raise ValueError('unit is not present')
            if args.cmd == 'boot':
                label = replace(label, partitions=tuple(replace(q, flags=(q.flags & ~BOOT) | (BOOT if q == p else 0)) for q in label.partitions))
            elif args.cmd == 'export':
                f.seek(p.start * 512)
                with args.image.open('xb') as dst:
                    copy_bytes(f, dst, p.used)
                return
            else:
                if args.image.resolve() == args.card.resolve() or os.path.samefile(args.image, args.card):
                    raise ValueError('input image is the card itself')
                size = args.image.stat().st_size
                if (p.kind != 6 and size != p.blocks * 512) or size > p.blocks * 512:
                    raise ValueError('input image size does not match partition')
                f.seek(p.start * 512)
                with args.image.open('rb') as src:
                    copy_bytes(src, f, size)
                f.flush()
                os.fsync(f.fileno())
                label = replace(label, partitions=tuple(replace(q, used=size) if q == p else q for q in label.partitions))
            label = replace(label, sequence=(label.sequence + 1) & 0xffffffff)
        publish(f, label)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, UnicodeError) as e:
        raise SystemExit(str(e))
