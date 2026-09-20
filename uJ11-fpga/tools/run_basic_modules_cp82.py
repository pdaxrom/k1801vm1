#!/usr/bin/env python3
"""Qualify FPP OFF/cold boot/restore with unmodified BASIC and ODT."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(out):
    out.mkdir(parents=True,exist_ok=True)
    assert not (out/'test.dsk').exists(),'fresh output directory required'
    baseline=ROOT/'build/cp81-basic/rtl-fis-abi'
    previous=json.loads((baseline/'inputs.json').read_text())
    for name,digest in previous['files'].items():assert sha(ROOT/name)==digest,name
    assert sha(baseline/'test.dsk')==previous['image_sha256']
    shutil.copyfile(baseline/'test.dsk',out/'test.dsk')
    retained=ROOT/'build/cp81-basic/rtl-error/fram-before.mem'
    cases=ROOT/'tb/basic_modules_cp82.vh'
    text=(baseline/'tb.v').read_text()
    text=text[:text.index('    initial begin\n        #1;')]
    text+=cases.read_text().replace('RETAINED',str(retained.relative_to(ROOT)))
    text=text.replace('CP81 progress','CP82 progress')
    (out/'tb.v').write_text(text)
    cmd=previous['compile'].copy()
    for i,arg in enumerate(cmd):
        if arg==str(baseline/'tb.v'):cmd[i]=str(out/'tb.v')
    assert str(out/'tb.v') in cmd
    cmd[cmd.index('--Mdir')+1]=str(out/'obj')
    runargs=[str(out/'obj/Vtb_odt_rt11'),'+SD_IMAGE='+str(out/'test.dsk'),'+UART_LOG='+str(out/'uart.txt')]
    files={n:sha(ROOT/n) for n in previous['files']}
    files.update({str(p.relative_to(ROOT)):sha(p) for p in (out/'tb.v',cases,Path(__file__))})
    inputs=dict(files=files,image_sha256=sha(out/'test.dsk'),compile=cmd,run=runargs,
                initialization='Retained external FRAM only; normal UART OFF/reinstall and cold bootstrap; no CPU forcing')
    (out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:subprocess.run(runargs,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    for n,h in files.items():assert sha(ROOT/n)==h,n
    assert sha(out/'test.dsk')==inputs['image_sha256']
    log=(out/'simulation.log').read_text()
    match=re.search(r'PASS CP82 modules: (\d+) checks, (\d+) clocks, (\d+) UART bytes',log);assert match
    result=dict(passed=True,checks=int(match[1]),clocks=int(match[2]),uart_bytes=int(match[3]),
                files={n:sha(out/n) for n in ('inputs.json','simulation.log','uart.txt','build.log')})
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(log[-2500:])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    args=p.parse_args();run(args.out.resolve())
