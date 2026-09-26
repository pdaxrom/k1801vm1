#!/usr/bin/env python3
"""Extract DEC LINK /R records; no instruction guessing or address scanning.

RT-11 Volume and File Formats Manual AA-PD6PA-TC, section 2.8.
Supported profile: one MODULE PSECT, no overlays, entry at its first word.
The module initializer supplies mutable data; only IMMEND is retained.
"""
import re
import struct
from board_common import ROOT
from rt11_build import build
from module_image import pack, relocate, LINK_BASE, validate_relocations


def extract(data, immutable_bytes, memory_bytes):
    def word(offset): return struct.unpack_from('<H', data, offset)[0]
    if len(data) < 1024 or len(data) % 512:
        raise ValueError('invalid DEC REL file extent')
    if (word(0o40) != LINK_BASE or word(0o56) != 0 or word(0o60) != 0o70524
            or word(0o52) != memory_bytes):
        raise ValueError('unsupported DEC REL layout, entry or root allocation')
    table = word(0o62)*512
    if not 512+memory_bytes <= table < len(data):
        raise ValueError('DEC REL table outside file or overlapping root')
    image = data[512:512+immutable_bytes]
    pairs = []
    previous = -1
    while table+2 <= len(data):
        offset = word(table); table += 2
        if offset == 0xfffe:
            validate_relocations(image, pairs)
            return image, pairs
        index = offset & 0x7fff
        if offset == 0xffff or table+2 > len(data) or index <= previous or index*2+2 > memory_bytes:
            raise ValueError('invalid/overlay/duplicate DEC REL record')
        original = word(table); table += 2
        if original != word(512+index*2):
            raise ValueError('DEC REL original word mismatch')
        if index*2 < immutable_bytes:
            pairs.append((offset, original))
        else:
            raise ValueError('mutable initialization must not contain relocations')
        previous = index
    raise ValueError('unterminated DEC REL records')


def build_module(source, base, immutable_bytes, memory_bytes, expected):
    """Link the same source as relocatable and compare at its reference address."""
    from build_software import sha
    text = source.read_text()
    text, origins = re.subn(r'^\s*\.\s*=\s*'+f'{base:o}'+r'\s*$', '', text, flags=re.M)
    if origins != 1:
        raise ValueError('expected one absolute origin')
    text, sections = re.subn(r'\.ASECT\b', '.PSECT MODULE,I,RW', text)
    if not sections or re.search(r'^\s*\.\s*=', text, re.M):
        raise ValueError('unsupported module section layout')
    src = source.parent/'RELMOD.MAC'; src.write_text(text)
    out = ROOT/'build/assembly'/('relmod-'+sha(src)[:12])
    if not (out/'build-inputs.json').exists():
        build([src], out, ROOT/'../lsi11-fpga/images/rt11v503.dsk', foreground=True)
    import json
    meta = json.loads((out/'build-inputs.json').read_text())
    assert meta['foreground'] and sha(src) in meta['source_sha256'].values()
    for name, digest in meta['outputs'].items():
        assert sha(out/name) == digest
    image, pairs = extract((out/'RELMOD.REL').read_bytes(), immutable_bytes, memory_bytes)
    if relocate(image, pairs, base) != expected:
        raise ValueError('relocated module differs from absolute reference build')
    return pack(base, image, memory_bytes=memory_bytes, relocations=pairs), out
