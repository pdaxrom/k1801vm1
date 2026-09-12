#!/usr/bin/env python3
"""Pack/validate ABI2 service files for UJLOAD.SAV (not raw FPGA ROM images)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

BLOCK = 512
SLOTS = {1: (0o10000, 0o40000), 2: (0o40000, 0x10000)}
MAGIC = (0x4a55, 0x3131)


def words(data):
    if len(data) % 2: raise ValueError('word data must have an even byte length')
    return struct.unpack('<'+'H'*(len(data)//2), data)


def decode(data):
    if len(data) < BLOCK or len(data) % BLOCK: raise ValueError('file must contain complete 512-byte blocks')
    h = words(data[:32])
    if h[:2] != MAGIC or h[2:4] != (1, 2): raise ValueError('magic, file version or ABI mismatch')
    kind, base, size, memory, entry, fault, checksum = h[4:11]
    if kind not in SLOTS or base != SLOTS[kind][0]: raise ValueError('module type or fixed load base')
    if not size or (size | memory | entry | fault) & 1: raise ValueError('empty or unaligned image')
    if not size <= memory <= SLOTS[kind][1]-base: raise ValueError('payload/BSS crosses its fixed slot')
    if not all(base <= pc < min(base+size, 0o160000) for pc in (entry, fault)):
        raise ValueError('entry/fault handler outside executable payload')
    if sum(h) & 65535 or any(data[24:BLOCK]): raise ValueError('header checksum, flags, reserved or padding')
    if len(data) != BLOCK + ((size+BLOCK-1)//BLOCK)*BLOCK: raise ValueError('file extent does not match header')
    payload = data[BLOCK:BLOCK+size]
    if sum(words(payload)) & 65535 != checksum: raise ValueError('payload checksum')
    if any(data[BLOCK+size:]): raise ValueError('payload padding')
    return dict(kind=kind, base=base, payload_bytes=size, memory_bytes=memory,
                entry=entry, fault=fault, checksum=checksum, file_blocks=len(data)//BLOCK,
                sha256=hashlib.sha256(data).hexdigest())


def pack(payload, kind, memory_bytes, entry, fault):
    if kind not in SLOTS: raise ValueError('kind must be 1 (ODT) or 2 (FP11)')
    h = [*MAGIC, 1, 2, kind, SLOTS[kind][0], len(payload), memory_bytes,
         entry, fault, sum(words(payload)) & 65535, 0, 0, 0, 0, 0]
    if any(not 0 <= value <= 65535 for value in h): raise ValueError('16-bit header field overflow')
    h[11] = (-sum(h)) & 65535
    data = struct.pack('<16H', *h) + bytes(BLOCK-32) + payload
    data += bytes((-len(data)) % BLOCK)
    decode(data)
    return data


def number(value):
    # Explicit prefixes avoid octal/decimal ambiguity in the host command line.
    return int(value, 0)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('file', type=Path)
    p.add_argument('--payload', type=Path, help='Pack this raw PDP-11 binary; without it, validate FILE')
    p.add_argument('--kind', choices=['odt', 'fp11'])
    p.add_argument('--memory-bytes', type=number)
    p.add_argument('--entry', type=number)
    p.add_argument('--fault', type=number)
    a = p.parse_args()
    if a.payload:
        if None in (a.kind, a.memory_bytes, a.entry, a.fault): p.error('packing requires kind, memory-bytes, entry and fault')
        if a.file.exists(): p.error('refusing to overwrite an existing service image')
        data = pack(a.payload.read_bytes(), 1 if a.kind == 'odt' else 2, a.memory_bytes, a.entry, a.fault)
        a.file.parent.mkdir(parents=True, exist_ok=True); a.file.write_bytes(data)
    print(json.dumps(decode(a.file.read_bytes()), indent=2))
