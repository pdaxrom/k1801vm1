#!/usr/bin/env python3
"""Differential execution of FP11 firmware on frozen CP67 core/ROM models."""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from board_common import ROOT
from build_fp11_cp68 import build, OUT
from build_modules_cp67 import adapt, OUT as HARDWARE
from run_service_cp57 import Program


def run(mode='sync', stride=1):
    out=OUT/mode;out.mkdir(parents=True,exist_ok=True)
    def atomic_text(path,text):
        temp=out/(path.name+'.tmp');temp.write_text(text);os.replace(temp,path)
    software = build()
    core, _ = adapt()
    sym = software['symbols']
    image = [0] * 65536
    raw = (OUT/'software/image.bin').read_bytes()
    for i in range(0, len(raw), 2):image[32768+(sym['INIT']+i)//2]=int.from_bytes(raw[i:i+2],'little')
    boot = Program(0o2400).mov(6,0o3700).mov(0,5).emit(0o42)
    boot.mov(0,0o7000).store(0,0o122).emit(0o4737,sym['INIT']).store(0,0o6004)
    boot.load(0,0o6000).store(0,sym['FPS']).load(0,0o6002).store(0,0o102)
    boot.mov(0,0o1000).store(0,0o100)
    for r in range(7):boot.load(r,0o6100+2*r)
    boot.emit(0o10)
    for i,w in enumerate(boot.words):image[32768+boot.base//2+i]=w
    atomic_text(OUT/'test-image.mem',''.join(f'{w:04x}\n' for w in image))
    atomic_text(OUT/'fp68_symbols.vh',''.join(f'localparam integer FP_{n}={sym[n]};\n' for n in ('FPS','FEC','FEA','ACS','ENTER','IMMEND','MEMEND')))
    oracle=out/'oracle'
    cc=['cc','-O2','-Wall','-DENABLE_MMU=0','-I..','tb/reference_fp_control_cp68.c','../core/core.c','../core/hardware.c','-lm','-o',str(oracle)]
    subprocess.run(cc,cwd=ROOT,check=True,capture_output=True)
    with (out/'vectors.tmp').open('w') as vector_log,(out/'oracle.log').open('w') as err:
        subprocess.run([str(oracle)],cwd=ROOT,stdout=vector_log,stderr=err,check=True)
    os.replace(out/'vectors.tmp',OUT/'vectors.mem')
    sources=['tb/tb_fp11_cp68.v']+core
    flags=[]
    if mode=='vendor':
        flags=['-DUJ11_VENDOR_ROM']
        sources += [str((HARDWARE/'uj11_m0_ebr.v').relative_to(ROOT))]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_fp11_cp68','-I'+str(OUT),'-o',str(out/'sim')]+flags+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp11_cp68',
             '-GROM_DECODE='+('0' if mode=='logic' else '1'),'--Mdir',str(out/'obj'),'-I'+str(OUT)]+sources
        sim=[str(out/'obj/Vtb_fp11_cp68')]
    files=sources+['firmware/fp11/FP11.MAC','tb/reference_fp_control_cp68.c','tools/test_fp11_cp68.py',
                   'build/cp68-fp11/test-image.mem','build/cp68-fp11/fp68_symbols.vh','build/cp68-fp11/vectors.mem',
                   '../core/core.c','../core/core.h','../core/pdp11_fp.c','../core/hardware.c','../core/hardware.h']
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}
    (out/'result.json').unlink(missing_ok=True)
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:
        result=subprocess.run(sim+[f'+STRIDE={stride}',f'+METRICS={out}/metrics.csv'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True)
    result.check_returncode()
    for p,digest in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest,p
    record=dict(passed=True,mode=mode,stride=stride,software=software,
                inputs=inputs)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode',choices=['sync','logic','vendor'],default='sync')
    p.add_argument('--stride',type=int,default=1)
    a=p.parse_args()
    if a.stride<1:p.error('stride must be positive')
    run(a.mode,a.stride)
