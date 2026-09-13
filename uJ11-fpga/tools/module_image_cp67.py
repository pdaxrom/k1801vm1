#!/usr/bin/env python3
"""CP67 type-free module file and four-word retained FRAM directory ABI."""
import struct

MAGIC = (0o45125, 0o30461)
VERSION, ABI = 2, 3
TABLE, SLOTS = 0o7000, 8
COOKIE, VALID, AUTO, OK, ERROR = 0xC100, 1, 2, 4, 8


def checksum(data):
    if len(data) % 2:
        raise ValueError('word-aligned image required')
    return sum(struct.unpack('<'+'H'*(len(data)//2), data)) & 65535


def bounds(base, words):
    end = base + words*2
    if (base & 1 or not 0 < words < 32768 or base < 0o6000 or
            end > 0o160000 or base < 0o10000 and end > 0o7000):
        raise ValueError('image outside executable module pools')
    return end


def pack(base, image):
    bounds(base, len(image)//2)
    header = [*MAGIC, VERSION, ABI, base, len(image)//2, checksum(image), 0]+[0]*8
    header[7] = -sum(header) & 65535
    result = struct.pack('<16H', *header)+bytes(480)+image
    return result+bytes(-len(result) % 512)


def decode(data):
    if len(data) < 1024 or len(data) % 512:
        raise ValueError('invalid RT-11 block extent')
    header = struct.unpack('<16H', data[:32])
    if header[:4] != (*MAGIC, VERSION, ABI) or checksum(data[:32]):
        raise ValueError('invalid module header')
    base, words, expected = header[4:7]
    bounds(base, words)
    size = 2*words
    if len(data) != 512 + ((size+511)//512)*512:
        raise ValueError('invalid RT-11 block extent')
    if any(data[16:512]) or any(data[512+size:]):
        raise ValueError('nonzero reserved fields or padding')
    image = data[512:512+size]
    if checksum(image) != expected:
        raise ValueError('module checksum mismatch')
    return dict(base=base, words=words, checksum=expected, bytes=size)


def entry(base, image, enabled=True):
    bounds(base, len(image)//2)
    return struct.pack('<4H', base, len(image)//2, checksum(image),
                       COOKIE | VALID | (AUTO if enabled else 0))
