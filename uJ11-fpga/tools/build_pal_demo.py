#!/usr/bin/env python3
"""Build a standalone PDP-11 PAL framebuffer/text/scroll demo (not an OS task)."""
import hashlib
import json
import re
from pathlib import Path
from board_common import ROOT
from build_software import native

def build():
    out=ROOT/'build/pal-demo';out.mkdir(parents=True,exist_ok=True)
    template=ROOT/'firmware/video/PALDEM.MAC.in'
    font_source=ROOT.parent/'pico-lsi11/pico-vt100/src/font5x7.c'
    data=re.sub(r'/\*.*?\*/','',font_source.read_text(),flags=re.S).split('= {',1)[1].split('};',1)[0]
    columns=[int(n,16) for n in re.findall(r'0x([0-9a-fA-F]+)',data)]
    assert len(columns)==96*5
    font=bytearray(128*8)
    for char in range(32,128):
        for y in range(7):
            font[char*8+y]=sum(((columns[(char-32)*5+x]>>y)&1)<<(x+1) for x in range(5))
    def bytes_asm(blob):
        return ''.join('        .BYTE '+','.join(f'{b:o}' for b in blob[i:i+16])+'\n' for i in range(0,len(blob),16))
    lines=['PAGE A - uJ11 HC7000 PAL framebuffer',
           '640x200: 80 columns / 320x200: 40 columns',
           'KEYS: 1 high, 2 low, S scroll, P page',
           'B colour grid, Q disable and HALT',
           '0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ',
           'abcdefghijklmnopqrstuvwxyz !@#$%&*()']
    while len(lines)<24:
        n=len(lines);lines.append(f'{n:02d} '+''.join(chr(32+(x+n)%95) for x in range(77)))
    lines.append('_ CURSOR / LAST TEXT ROW')
    text=''.join(s.ljust(80)[:80] for s in lines).encode('ascii')
    table=[sum(15<<(4*i) for i in range(4) if n&(1<<i)) for n in range(16)]
    asm=template.read_text().replace('@@FONT@@',bytes_asm(font)).replace('@@TEXT@@',bytes_asm(text))
    asm=asm.replace('@@NIBBLES@@','        .WORD '+','.join(f'{v:o}' for v in table)+'\n')
    src=out/'PALDEM.MAC';src.write_text(asm)
    raw,symbols,assembly,directory=native(src)
    assert symbols['START']==0o20000 and symbols['IMEND']<0o40000
    binary=raw[0o20000:symbols['IMEND']]
    (out/'paldem.bin').write_bytes(binary)
    (out/'paldem.bytes').write_text(''.join(f'{b:02x}\n' for b in binary))
    (out/'paldem.mem').write_text(''.join(f'{int.from_bytes(binary[i:i+2],"little"):04x}\n' for i in range(0,len(binary),2)))
    record=dict(start_octal='020000',end_octal=f'{symbols["IMEND"]:06o}',bytes=len(binary),
        standalone_only=True,framebuffer_bytes=[0x1e0000,0x1f0000],symbols=symbols,
        sources={str(p.relative_to(ROOT.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                 (template,font_source,Path(__file__))},assembly=str(directory),assembler=assembly,
        sha256=hashlib.sha256(binary).hexdigest())
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:v for k,v in record.items() if k not in ('symbols','sources','assembler')},indent=2))
    return record
if __name__=='__main__':build()
