#!/usr/bin/env python3
"""Convert physical Terak IMD media to an SDHC card without losing sector flags."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from sdcard import Label, add_partition, publish, read_label

TRACKS, SECTORS, SIZE = 77, 26, 128
COUNT, BYTES = TRACKS*SECTORS, TRACKS*SECTORS*SIZE
MAGIC = b'UJ11FD1\0'
STRIDE, TRACK_BYTES = 33, 16384


def crc_block(data):
    block = bytearray(data)
    struct.pack_into('<I', block, 508, zlib.crc32(block[:508]))
    return bytes(block)


def decode_imd(data):
    """Physical track/side records, including the mixed-density index cylinder."""
    if not data.startswith(b'IMD ') or b'\x1a' not in data:
        raise ValueError('Expected an ImageDisk image')
    position = data.index(b'\x1a')+1
    tracks = {}
    def take(n):
        nonlocal position
        result = data[position:position+n]
        if len(result) != n:
            raise ValueError('Truncated IMD record')
        position += n
        return result
    while position < len(data):
        mode, cylinder, head, count, size = take(5)
        side = head & 63
        if mode not in (0, 3) or side > 1 or cylinder >= TRACKS or not 1 <= count <= 32 or size > 6:
            raise ValueError('Expected 8-inch FM/MFM media, N=0..6, 1..32 sectors')
        length = 128 << size
        if count*length > TRACK_BYTES or (cylinder, side) in tracks:
            raise ValueError('Duplicate/oversize physical track')
        numbers = take(count)
        cylinders = take(count) if head & 128 else bytes([cylinder]*count)
        heads = take(count) if head & 64 else bytes([side]*count)
        if len(set(numbers)) != count or any(not n for n in numbers):
            raise ValueError('Invalid sector numbering')
        sectors = []
        for number, c, h in zip(numbers, cylinders, heads):
            kind = take(1)[0]
            if kind > 8:
                raise ValueError('Unknown IMD sector type')
            payload = bytes(length) if not kind else take(1)*length if not kind & 1 else take(length)
            flags = (1 | (2 if kind in (3, 4, 7, 8) else 0) | (4 if kind >= 5 else 0)) if kind else 0
            sectors.append((c, h, number, size, flags, payload))
        # Canonical physical IDs, never logical UCSD sector skew.
        tracks[cylinder, side] = (mode == 3, sorted(sectors, key=lambda s: s[2]))
    heads = 2 if any(h for _, h in tracks) else 1
    if set(tracks) != {(c, h) for c in range(TRACKS) for h in range(heads)}:
        raise ValueError('IMD is missing physical tracks')
    return heads, tracks


def imd(data):
    """Legacy helper for single-sided 77x26x128 fixtures and provenance."""
    heads, tracks = decode_imd(data)
    payload, flags = bytearray(), bytearray()
    if heads != 1:
        raise ValueError('Expected single-sided FM 77x26x128 media')
    for c in range(TRACKS):
        mfm, sectors = tracks[c, 0]
        if mfm or len(sectors) != 26 or any(s[:4] != (c, 0, i+1, 0) for i, s in enumerate(sectors)):
            raise ValueError('Expected single-sided FM 77x26x128 media')
        for _, _, _, _, flag, data in sectors:
            payload.extend(data); flags.append(flag)
    return bytes(payload), bytes(flags)


def floppy(data):
    heads, tracks = decode_imd(data)
    image = bytearray((1+77*heads*STRIDE)*512)
    header = bytearray(512)
    header[:8] = MAGIC
    struct.pack_into('<5I', header, 8, 1, 77, heads, STRIDE, len(image)//512)
    image[:512] = crc_block(header)
    for (c, h), (mfm, sectors) in tracks.items():
        start = (1+(c*heads+h)*STRIDE)*512
        meta = bytearray(512)
        meta[:8] = b'TRK1'+bytes([len(sectors), sectors[0][3], int(mfm), 0])
        offset = start+512
        for i, (ic, ih, r, n, flag, payload) in enumerate(sectors):
            meta[32+i*8:37+i*8] = bytes([ic, ih, r, n, flag])
            image[offset:offset+len(payload)] = payload
            offset += len(payload)
        image[start:start+512] = crc_block(meta)
    return bytes(image), heads


def card(paths, output, readonly=()):
    if not 1 <= len(paths) <= 4 or any(u not in range(len(paths)) for u in readonly):
        raise ValueError('Provide one to four disks and valid readonly units')
    images = [floppy(path.read_bytes()) for path in paths]
    # Use the SAME allocator, metadata and publishing path as other uJ11 disks.
    label = Label(32768)
    records = []
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w+b') as stream:
        stream.truncate(label.blocks*512)
        for unit, (path, (data, heads)) in enumerate(zip(paths, images)):
            label = add_partition(label, 'terak-ss' if heads == 1 else 'terak-ds', unit,
                                  readonly=unit in readonly, name=path.stem[:8])
            p = label.partitions[-1]
            stream.seek(p.start*512); stream.write(data)
            records.append(dict(unit=unit, source=str(path.resolve()), heads=heads, start=p.start,
                                blocks=p.blocks, readonly=unit in readonly,
                                source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                payload_sha256=hashlib.sha256(data).hexdigest()))
        publish(stream, label)
    record = dict(format='UJ11SD', media_format='UJ11FD1', blocks=label.blocks, disks=records,
                  image_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
    output.with_suffix('.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=Path, action='append', help='IMD disks in QB/QX unit order')
    parser.add_argument('--readonly', type=int, action='append', default=[])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--floppy', type=Path, help='Convert one IMD to a partition payload for sdcard.py import')
    args = parser.parse_args()
    if args.floppy:
        args.out.write_bytes(floppy(args.floppy.read_bytes())[0])
    elif args.disk:
        print(json.dumps(card(args.disk, args.out, args.readonly), indent=2))
    else:
        parser.error('provide --disk or --floppy')
