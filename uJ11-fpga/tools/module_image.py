#!/usr/bin/env python3
"""CP67 type-free module file and four-word retained FRAM directory ABI."""
import struct

MAGIC = (0o45125, 0o30461)
VERSION, ABI = 2, 3
ALLOCATION_VERSION = 3
RELOCATION_VERSION, LINK_BASE = 4, 0o1000
TABLE, SLOTS = 0o7000, 8
ALLOCATION_TABLE, ALLOCATION_MAGIC = 0o7100, 0o110521
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


def pack(base, image, *, memory_bytes=None, relocations=None):
    bounds(base, len(image)//2)
    header = [*MAGIC, VERSION, ABI, base, len(image)//2, checksum(image), 0]+[0]*8
    if memory_bytes is not None:
        if memory_bytes & 1 or memory_bytes < len(image):
            raise ValueError('allocation must include the word-aligned immutable image')
        bounds(base, memory_bytes//2)
        header[2] = ALLOCATION_VERSION
        header[8] = memory_bytes//2
    trailer = b''
    if relocations is not None:
        if memory_bytes is None:
            raise ValueError('relocatable module requires its full allocation')
        validate_relocations(image, relocations)
        trailer = b''.join(struct.pack('<2H', *pair) for pair in relocations)
        header[2] = RELOCATION_VERSION
        header[9:12] = [LINK_BASE, len(relocations), checksum(trailer)]
    header[7] = -sum(header) & 65535
    result = struct.pack('<16H', *header)+bytes(480)+image
    result += bytes(-len(result) % 512)
    return result+trailer+bytes(-len(trailer) % 512)


def validate_relocations(image, relocations):
    previous = -1
    for offset, original in relocations:
        index = offset & 0x7fff
        if not 0 <= offset <= 65535 or not 0 <= original <= 65535:
            raise ValueError('relocation word out of range')
        if index <= previous or 2*index+2 > len(image):
            raise ValueError('relocations must be unique, ordered and inside immutable image')
        if int.from_bytes(image[2*index:2*index+2], 'little') != original:
            raise ValueError('relocation original differs from image')
        previous = index


def relocate(image, relocations, base):
    validate_relocations(image, relocations)
    result = bytearray(image)
    delta = base-LINK_BASE
    for offset, original in relocations:
        value = original + (-delta if offset & 0x8000 else delta)
        struct.pack_into('<H', result, 2*(offset & 0x7fff), value & 65535)
    return bytes(result)


def decode(data):
    if len(data) < 1024 or len(data) % 512:
        raise ValueError('invalid RT-11 block extent')
    header = struct.unpack('<16H', data[:32])
    if (header[:2] != MAGIC or header[2] not in (VERSION, ALLOCATION_VERSION, RELOCATION_VERSION)
            or header[3] != ABI or checksum(data[:32])):
        raise ValueError('invalid module header')
    base, words, expected = header[4:7]
    bounds(base, words)
    size = 2*words
    image_end = 512 + ((size+511)//512)*512
    count = header[10] if header[2] == RELOCATION_VERSION else 0
    if len(data) != image_end + ((count*4+511)//512)*512:
        raise ValueError('invalid RT-11 block extent')
    reserved = {VERSION:16, ALLOCATION_VERSION:18, RELOCATION_VERSION:24}[header[2]]
    if any(data[reserved:512]) or any(data[512+size:image_end]):
        raise ValueError('nonzero reserved fields or padding')
    image = data[512:512+size]
    if checksum(image) != expected:
        raise ValueError('module checksum mismatch')
    result = dict(base=base, words=words, checksum=expected, bytes=size)
    if header[2] >= ALLOCATION_VERSION:
        bounds(base, header[8])
        if header[8] < words:
            raise ValueError('allocation is smaller than the image')
        result.update(memory_bytes=2*header[8], memory_words=header[8], version=header[2])
    if header[2] == RELOCATION_VERSION:
        if header[9] != LINK_BASE or count > words:
            raise ValueError('invalid relocation origin or count')
        trailer = data[image_end:image_end+count*4]
        if checksum(trailer) != header[11] or any(data[image_end+count*4:]):
            raise ValueError('relocation checksum or padding mismatch')
        pairs = list(struct.iter_unpack('<2H', trailer))
        validate_relocations(image, pairs)
        result.update(link_base=LINK_BASE, relocations=pairs)
    return result


def placed_image(data, base=None):
    record = decode(data)
    target = record['base'] if base is None else base
    bounds(target, record.get('memory_words', record['words']))
    image = data[512:512+record['bytes']]
    if record.get('version') == RELOCATION_VERSION:
        return relocate(image, record['relocations'], target)
    if target != record['base']:
        raise ValueError('absolute module cannot move')
    return image


def allocation_entry(base, image, memory_bytes):
    """Loader sidecar, bound to the ROM directory's immutable identity.

    Eight words per slot at 007100. ROM still sees its original four words.
    The final word makes the sum zero; incomplete/stale metadata is rejected.
    """
    decode(pack(base, image, memory_bytes=memory_bytes))
    words = [ALLOCATION_MAGIC, base, len(image)//2, checksum(image),
             memory_bytes//2, ALLOCATION_VERSION, 0, 0]
    words[-1] = -sum(words) & 65535
    return struct.pack('<8H', *words)


def entry(base, image, enabled=True):
    bounds(base, len(image)//2)
    return struct.pack('<4H', base, len(image)//2, checksum(image),
                       COOKIE | VALID | (AUTO if enabled else 0))
