#!/usr/bin/env python3
"""Run the actual PDP-11 demo, SERV and PAL DMA; compare every frame byte."""
import hashlib
import json
import struct
import subprocess
import zlib
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build,CORE,BOARD
from build_pal_demo import build as demo_build
from serv_test import GUARD,memory_args

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def expected(demo,mode,page):
    raw=(ROOT/'build/pal-demo/paldem.bin').read_bytes()
    sym=demo['symbols'];base=0o20000
    font=raw[sym['FONT']-base:sym['FONT']-base+1024]
    text=bytearray(raw[sym['TEXT']-base:sym['TEXT']-base+2000]);text[5]=ord(page)
    pixels=[];cols=80 if mode==4 else 40
    for y in range(200):
        for x in range(cols):
            bits=font[8*text[(y//8)*80+x]+y%8]
            pixels.extend(15 if bits&(1<<b) else 0 for b in range(8))
    data=bytes(pixels) if mode==8 else bytes(pixels[i]|(pixels[i+1]<<4) for i in range(0,len(pixels),2))
    return data.ljust(65536,b'\0')
def png(path,blob):
    # Lossless nearest-neighbour view of the digital frame, not a TV capture.
    rows=[]
    for y in range(200):
        row=bytearray()
        for b in blob[y*320:(y+1)*320]:
            for v in (b&15,b>>4):row.extend(bytes([255 if v else 0])*6)
        rows.extend([b'\0'+row]*2)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1280,400,8,2,0,0,0))+
                     chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))
def run():
    hw=build();demo=demo_build();out=ROOT/'build/test-pal-demo';out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+BOARD+[GUARD,'tests/mmu/tb_pal_demo.v','tests/models/async_sram_model.v']
    hashes={p:sha(ROOT/p) for p in inventory+['tools/test_pal_demo.py']}
    with (out/'build.log').open('w') as log:
        subprocess.run(['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
            '--top-module','tb_pal_demo','-j','4','--Mdir',str(out/'obj')]+inventory,
            cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        r=subprocess.run([str(out/'obj/Vtb_pal_demo'),f'+OUT={out}']+memory_args(hw),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:]);r.check_returncode()
    for mode,name in [(4,'640.bin'),(8,'320.bin')]:
        want=expected(demo,mode,'A')+expected(demo,mode,'B');got=(out/name).read_bytes()
        if want!=got:
            first=next(i for i,(a,b) in enumerate(zip(want,got)) if a!=b)
            raise AssertionError(f'{name} pixel mismatch {first:#x}: {got[first]:02x} expected {want[first]:02x}')
    want=bytes(2560)+expected(demo,4,'A')[2560:]+expected(demo,4,'B')
    assert (out/'scroll.bin').read_bytes()==want,'Ring scroll must clear only 2560 bytes on page A'
    assert 'PASS PAL PDP-11 demo' in (out/'simulation.log').read_text()
    assert all(sha(ROOT/p)==v for p,v in hashes.items())
    png(out/'text-80x25.png',(out/'640.bin').read_bytes()[:64000])
    (out/'result.json').write_text(json.dumps(dict(passed=True,files=hashes,hardware=hw,demo=demo,
        checked_frame_bytes=3*131072),indent=2)+'\n')
    print('PASS PAL demo: 393216 SRAM bytes match text and ring-scroll reference')
if __name__=='__main__':run()
