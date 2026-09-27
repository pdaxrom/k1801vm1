#!/usr/bin/env python3
"""Build the HC7000 SD menu with the repo's assembler in an isolated directory."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from board_common import ROOT
from build_mmu_board import sha
from storage_menu import pack,unpack

def build(out):
    out.mkdir(parents=True,exist_ok=True)
    assembler=ROOT.parent/'microasm11/microasm11.c'
    source=ROOT/'firmware/boot/SDMENU.asm'
    executable=out/'microasm11'
    subprocess.run([os.environ.get('CC','cc'),'-O2',str(assembler),'-o',str(executable)],check=True)
    binary=out/'menu.bin'
    listing=out/'menu.lst'
    subprocess.run([str(executable),'-binary','--cpu','dcj-11','--list',str(listing),str(source),str(binary)],check=True)
    data=binary.read_bytes()
    image=pack(data);(out/'menu.img').write_bytes(image)
    record=dict(format=unpack(image),program_bytes=len(data),
        sources={str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else '../'+str(p.relative_to(ROOT.parent)):sha(p)
                 for p in (assembler,source,Path(__file__),ROOT/'tools/storage_menu.py')},
        outputs={p:sha(out/p) for p in ('menu.bin','menu.img','menu.lst')})
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record['format']|{'program_bytes':len(data),'image':str(out/'menu.img')},indent=2))
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/storage-menu')
    build(p.parse_args().out.resolve())
