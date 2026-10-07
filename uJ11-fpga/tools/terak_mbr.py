#!/usr/bin/env python3
"""TERAK MBR cards: FAT16 configuration plus direct raw/UJ11FD1 disk partitions.

Only regular image files are accepted. No physical-card writes or formatting.
"""
import argparse
import hashlib
import json
import stat
import struct
import zlib
from pathlib import Path

from terak_image import decode_imd, floppy, MAGIC

PROFILES = ('sssd', 'dssd', 'ssdd', 'dsdd', 'terak-ssdd', 'terak-dsdd')
ALIGN, FAT_BLOCKS = 2048, 32768


def geometry(profile, cylinder, head):
    if profile not in PROFILES or not 0 <= cylinder < 77 or head not in range(heads(profile)):
        raise ValueError('Invalid raw profile or track')
    if profile.startswith('terak-') and cylinder == 0:
        return 26, head, bool(head)
    return (26, 0, False) if profile in ('sssd', 'dssd') else (15, 2, True)


def heads(profile):
    return 2 if profile in ('dssd', 'dsdd', 'terak-dsdd') else 1


def raw_size(profile):
    return sum(count * (128 << n) for c in range(77) for h in range(heads(profile))
               for count, n, _ in [geometry(profile, c, h)])


def raw_from_imd(data, profile, discard_status=False):
    sides, tracks = decode_imd(data)
    if sides != heads(profile):
        raise ValueError('Raw profile does not match IMD sides')
    out = bytearray()
    for (c, h), (mfm, sectors) in sorted(tracks.items()):
        count, n, density = geometry(profile, c, h)
        if len(sectors) != count or mfm != density or any(s[:4] != (c, h, i+1, n) for i, s in enumerate(sectors)):
            raise ValueError('Raw profile does not match IMD CHRN/density')
        if not discard_status and any(s[4] != 1 for s in sectors):
            raise ValueError('Raw would lose IMD status; explicitly use --discard-status')
        out.extend(b''.join(s[5] for s in sectors))
    return bytes(out)


def fd_size(data):
    if len(data) < 512 or data[:8] != MAGIC or zlib.crc32(data[:508]) != struct.unpack_from('<I', data, 508)[0]:
        raise ValueError('Invalid UJ11FD1 header/CRC')
    version, cylinders, sides, stride, blocks = struct.unpack_from('<5I', data, 8)
    if (version, cylinders, stride) != (1, 77, 33) or sides not in (1, 2) or blocks != 1+77*sides*33:
        raise ValueError('Invalid UJ11FD1 geometry')
    return blocks*512


def payload(path, profile, discard_status=False):
    data = Path(path).read_bytes()
    if profile == 'fd':
        if data.startswith(b'IMD '):
            data = floppy(data)[0]
        expected = fd_size(data)
    else:
        if data.startswith(b'IMD '):
            data = raw_from_imd(data, profile, discard_status)
        expected = raw_size(profile)
    if len(data) != expected:
        raise ValueError(f'{profile}: expected {expected} bytes, got {len(data)}')
    return data


def fat16(config=None):
    # Standard 16 MiB FAT16, 512-byte clusters, two FAT copies, fixed root.
    spf, root_entries = 127, 128
    data_start = 1+2*spf+root_entries//16
    volume = bytearray(FAT_BLOCKS*512)
    volume[:11] = b'\xeb\x3c\x90UJ11TRAK'
    struct.pack_into('<HBHBHHBHHHII', volume, 11, 512, 1, 1, 2, root_entries, FAT_BLOCKS,
                     0xf8, spf, 32, 2, ALIGN, 0)
    volume[38] = 0x29
    struct.pack_into('<I', volume, 39, 0x54455241)
    volume[43:54], volume[54:62], volume[510:512] = b'TERAK      ', b'FAT16   ', b'\x55\xaa'
    table = bytearray(spf*512)
    struct.pack_into('<HH', table, 0, 0xfff8, 0xffff)
    if config is not None:
        config = config.encode('ascii') if isinstance(config, str) else bytes(config)
        if not 1 <= len(config) <= 512:
            raise ValueError('TERAK.CFG must fit one 512-byte sector')
        struct.pack_into('<H', table, 4, 0xffff)
        root = (1+2*spf)*512
        volume[root:root+11] = b'TERAK   CFG'
        volume[root+11] = 0x20
        struct.pack_into('<HI', volume, root+26, 2, len(config))
        volume[data_start*512:data_start*512+len(config)] = config
    for i in range(2):
        volume[(1+i*spf)*512:(1+(i+1)*spf)*512] = table
    label = (1+2*spf)*512 + (32 if config is not None else 0)
    volume[label:label+11] = b'TERAK      '
    volume[label+11] = 8
    return bytes(volume)


def put_entry(sector, index, kind, start, blocks):
    struct.pack_into('<B3sB3sII', sector, 446+index*16, 0, b'\xfe\xff\xff', kind,
                     b'\xfe\xff\xff', start, blocks)
    sector[510:512] = b'\x55\xaa'


def align(value):
    return (value+ALIGN-1)//ALIGN*ALIGN


def create(disks, output, readonly=(), logical=False, config=True, discard_status=False):
    if not 1 <= len(disks) <= 4 or any(u not in range(len(disks)) for u in readonly):
        raise ValueError('Provide 1..4 disks and valid readonly drive numbers')
    images = [payload(path, profile, discard_status) for profile, path in disks]
    mbr, regions, records = bytearray(512), [], []
    put_entry(mbr, 0, 6, ALIGN, FAT_BLOCKS)
    cursor = ALIGN+FAT_BLOCKS
    extended_start = 0
    for u, ((profile, path), data) in enumerate(zip(disks, images)):
        is_logical = logical or u >= 2 and len(disks) == 4
        if is_logical:
            ebr = bytearray(512)
            at, start = align(cursor), align(cursor)+ALIGN
            if not extended_start:
                extended_start = at
            if regions and regions[-1][2] == 'ebr':
                raise AssertionError('Invalid builder order')
            if records and records[-1].get('ebr') is not None:
                previous = next(v for p, v, kind in regions if p == records[-1]['ebr'] and kind == 'ebr')
                # Link length is set after all extents have been allocated.
                put_entry(previous, 1, 15, at-extended_start, 1)
            number = 5 + sum('ebr' in r for r in records)
            regions.append((at, ebr, 'ebr'))
        else:
            start, number = align(cursor), u+2
        blocks = (len(data)+511)//512
        if is_logical:
            put_entry(ebr, 0, 0xda, start-at, blocks)
        else:
            put_entry(mbr, u+1, 0xda, start, blocks)
        record = dict(unit=u, partition=number, start=start, blocks=blocks, bytes=len(data),
                      format=profile, readonly=u in readonly, source=str(Path(path).resolve()),
                      payload_sha256=hashlib.sha256(data).hexdigest())
        if is_logical:
            record['ebr'] = at
        records.append(record)
        regions.append((start, data, 'disk'))
        cursor = start+blocks
    if extended_start:
        put_entry(mbr, 3, 15, extended_start, cursor-extended_start)
        for _, ebr, kind in regions:
            if kind == 'ebr' and ebr[466]:
                relative = struct.unpack_from('<I', ebr, 470)[0]
                struct.pack_into('<I', ebr, 474, cursor-extended_start-relative)
    lines = ['version=1']
    for r in records:
        form = 'fd' if r['format'] == 'fd' else 'raw,'+r['format']
        lines.append(f"drive{r['unit']}={r['partition']},{form},{'ro' if r['readonly'] else 'rw'}")
    for u in range(len(records), 4):
        lines.append(f'drive{u}=off')
    if not config and any(p != 'fd' for p, _ in disks):
        raise ValueError('Raw media requires TERAK.CFG')
    volume = fat16('\n'.join(lines)+'\n' if config else None)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.truncate(align(cursor)*512)
        f.write(mbr)
        f.seek(ALIGN*512)
        f.write(volume)
        for start, data, _ in regions:
            f.seek(start*512)
            f.write(data)
    result = dict(format='MBR', blocks=align(cursor), disks=records, config='\n'.join(lines)+'\n' if config else None,
                  image_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
    output.with_suffix('.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def partitions(stream, maximum=16):
    if maximum not in (12, 16):
        raise ValueError("Partition limit must be 12 or 16")
    stream.seek(0, 2)
    capacity = stream.tell()//512
    def read(lba):
        stream.seek(lba*512)
        data = stream.read(512)
        if len(data) != 512 or data[510:] != b'\x55\xaa':
            raise ValueError('Invalid MBR/extended signature')
        return data
    def entry(data, index, base=0, limit=capacity):
        p = data[446+index*16:462+index*16]
        kind, start, blocks = p[4], *struct.unpack_from('<II', p, 8)
        if not kind and not start and not blocks:
            return None
        if p[0] not in (0, 128) or not kind or kind == 0xee or not start or not blocks or base+start+blocks > limit:
            raise ValueError('Invalid partition extent')
        return dict(type=kind, start=base+start, blocks=blocks)
    result = {i+1: entry(read(0), i) for i in range(4)}
    present = [p for p in result.values() if p]
    for i, p in enumerate(present):
        if any(p['start'] < q['start']+q['blocks'] and q['start'] < p['start']+p['blocks'] for q in present[:i]):
            raise ValueError('Overlapping partitions')
    if any(p and p['type'] in (5, 15, 133) for n, p in result.items() if n != 4):
        raise ValueError('Extended container must be partition 4')
    container = result[4]
    if container and container['type'] in (5, 15, 133):
        base, end = container['start'], container['start']+container['blocks']
        current = base
        for n in range(5, maximum+1):
            data = read(current)
            p, link = entry(data, 0, current, end), entry(data, 1, base, end)
            if not p or p['type'] in (5, 15, 133) or any(data[478:510]):
                raise ValueError('Invalid logical partition')
            result[n] = p
            if not link:
                break
            if link['type'] not in (5, 15, 133) or link['start'] < p['start']+p['blocks'] or n == maximum:
                raise ValueError('Invalid or excessive extended chain')
            current = link['start']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    new = sub.add_parser('create')
    new.add_argument('--disk', action='append', required=True, metavar='PROFILE:FILE')
    new.add_argument('--out', type=Path, required=True)
    new.add_argument('--readonly', type=int, action='append', default=[])
    new.add_argument('--logical', action='store_true')
    new.add_argument('--no-config', action='store_true')
    new.add_argument('--discard-status', action='store_true')
    for name in ('list', 'import', 'export'):
        p = sub.add_parser(name)
        p.add_argument('image', type=Path)
        if name != 'list':
            p.add_argument('--partition', type=int, required=True)
            p.add_argument('--profile', choices=('fd',)+PROFILES, required=True)
            p.add_argument('--file', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'create':
        disks = [item.split(':', 1) for item in args.disk]
        if any(len(d) != 2 or d[0] not in ('fd',)+PROFILES for d in disks):
            parser.error('Disk must be PROFILE:FILE; profiles: fd, '+', '.join(PROFILES))
        print(json.dumps(create(disks, args.out, args.readonly, args.logical, not args.no_config, args.discard_status), indent=2))
        return
    with args.image.open('r+b' if args.command == 'import' else 'rb') as stream:
        if not stat.S_ISREG(__import__('os').fstat(stream.fileno()).st_mode):
            raise ValueError('Only regular SD image files are supported')
        table = partitions(stream)
        if args.command == 'list':
            print(json.dumps(table, indent=2))
            return
        p = table.get(args.partition)
        if args.partition == 1 or not p or p['type'] != 0xda:
            raise ValueError('Select a raw data partition, excluding FAT16/extended metadata')
        if args.command == 'import':
            data = payload(args.file, args.profile)
            if (len(data)+511)//512 > p['blocks']:
                raise ValueError('Image does not fit partition')
            stream.seek(p['start']*512)
            stream.write(data)
        else:
            stream.seek(p['start']*512)
            size = fd_size(stream.read(512)) if args.profile == 'fd' else raw_size(args.profile)
            if (size+511)//512 > p['blocks']:
                raise ValueError('Image does not fit partition')
            stream.seek(p['start']*512)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError('Truncated SD image')
            with args.file.open('xb') as output:
                output.write(data)


if __name__ == '__main__':
    main()
