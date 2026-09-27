#!/usr/bin/env python3
"""Boot-menu test at actual CPU/SPI/UART pins, with a faster test-only KW11 tick."""
import argparse
from dataclasses import replace
import json
from pathlib import Path
import struct
import subprocess
from board_common import ROOT
from build_mmu_board import build,CORE,BOARD,sha
from build_storage_menu import build as build_menu
from sdcard import Label,add_partition,publish,MENU,main as sdcard
from storage_menu import crc16

def fixture(path,menu,mode):
    media='rm05' if mode=='xp-auto' else 'rl02' if mode=='rl-auto' else 'rk07'
    unit=1 if mode=='rl-auto' else 7
    label=add_partition(Label(1048576 if mode=='xp-auto' else 131072),media,0)
    label=add_partition(label,media,unit,boot=mode!='no-default')
    with path.open('w+b') as f:
        f.truncate(label.blocks*512);publish(f,label)
        for p in label.partitions:
            # Boot sector records the selected unit, a distinct per-media
            # signature, and the controller ABI, then spins in guest code.
            words=[0o010037,0o1000,0o012737,0o12345+p.unit,0o1002,0o010137,0o1004,0o000777]
            f.seek(p.start*512);f.write(struct.pack('<8H',*words))
    if mode!='direct':sdcard([str(path),'menu',str(menu)])
    if mode in ('bad-header','bad-payload','bad-size'):
        with path.open('r+b') as f:
            if mode=='bad-payload':f.seek(3*512+12);f.write(b'\xff')
            else:
                f.seek(2*512);hdr=bytearray(f.read(512))
                if mode=='bad-header':hdr[24]^=1
                else:
                    struct.pack_into('<H',hdr,8,17)
                    hdr[510:512]=crc16(hdr[:510]).to_bytes(2,'big')
                f.seek(2*512);f.write(hdr)

CASES=('auto','rl-auto','xp-auto','enter','cancel','no-default','direct','bad-header','bad-payload','bad-size','bad-wire')

def run(out,only=None):
    record=build();out.mkdir(parents=True,exist_ok=True)
    record['menu']=build_menu(out/'menu')
    inventory=CORE+BOARD+['tests/mmu/tb_storage_menu.v','tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    record['test_files']={p:sha(ROOT/p) for p in inventory+['tools/test_storage_menu.py','tools/sdcard.py','tools/storage_menu.py']}
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD','--top-module','tb_storage_menu','-j','4','--Mdir',str(out/'obj')]+inventory
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    cases=[]
    for mode in ([only] if only else CASES):
        image=out/(mode+'.img');fixture(image,out/'menu/menu.img',mode);before=sha(image)
        cmd=[str(out/'obj/Vtb_storage_menu'),f'+SD_IMAGE={image}',f'+UART_LOG={out}/{mode}-uart.txt',f'+MODE={mode}']
        with (out/(mode+'.log')).open('w') as log:result=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        text=(out/(mode+'.log')).read_text();print(text[-1600:],flush=True)
        result.check_returncode()
        assert 'PASS MMU' in text and sha(image)==before
        cases.append(dict(mode=mode,image_sha256=before))
    assert all(sha(ROOT/p)==h for p,h in record['test_files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record,cases=cases),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-storage-menu')
    p.add_argument('--case',choices=CASES)
    a=p.parse_args();run(a.out.resolve(),a.case)
