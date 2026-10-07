#!/usr/bin/env python3
"""Build the optional guest BIOS, loaded from FAT16 by SERV before CPU release."""
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path

from board_common import ROOT


def build(out, hard_disk=False):
    out.mkdir(parents=True, exist_ok=True)
    template = ROOT/'firmware/terak/bios.asm'
    font_source = ROOT/'firmware/storage/terminal_font.h'
    glyphs = re.findall(r'\{\s*((?:0x[0-9a-f]{2}[,\s]*){8})\}', font_source.read_text())
    if len(glyphs) != 96:
        raise ValueError('Expected the existing 96 printable VT52 glyphs')
    font = '\n'.join('        db 0,'+','.join(re.findall(r'0x[0-9a-f]{2}', g))+',0' for g in glyphs)
    source = out/'bios.asm'
    text = template.read_text().replace('; @FONT@', font)
    if hard_disk:
        text = text.replace('        mov #help,r0', '        mov #hd,r0\n        mov #162620,r1\n'
                            '        jsr pc,print\n        mov #help,r0')
        text = text.replace('cmp r2,#3', 'cmp r2,#4').replace('PRESS 1-4', 'PRESS 1-5')
        text = text.replace('help:   db', 'hd:     db "5  RT-11 5.7 XM / RL",0\nhelp:   db')
    source.write_text(text)
    assembler = os.environ.get('MICROASM11', str(ROOT.parent/'microasm11/microasm11'))
    binary = out/'bios.bin'
    subprocess.run([assembler, '-binary', str(source), str(binary)], check=True)
    data = binary.read_bytes()
    if not data or len(data) > 4096 or len(data) % 2:
        raise ValueError('Primary BIOS must be an even-sized <=4 KiB image')
    checksum = sum(struct.unpack('<'+'H'*(len(data)//2), data)) & 65535
    image = struct.pack('<4sIII', b'UJTB', len(data), checksum, 1)+data
    (out/'TBIOS.BIN').write_bytes(image)
    record = dict(bytes=len(data), checksum=checksum, sha256=hashlib.sha256(image).hexdigest(), hard_disk=hard_disk,
                  files={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (template, font_source, Path(__file__))})
    (out/'bios.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    print(json.dumps(build(parser.parse_args().out), indent=2))
