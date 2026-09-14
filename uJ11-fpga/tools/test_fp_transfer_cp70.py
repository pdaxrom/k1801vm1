#!/usr/bin/env python3
"""Differential word/status addressing and fault tests on frozen CP67 hardware."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT
from build_fp11_cp70 import build, OUT
from build_modules_cp67 import adapt, OUT as HW
from run_service_cp57 import Program


def run(mode='sync'):
    fp=build();core,_=adapt();s=fp['symbols'];out=OUT/mode;out.mkdir(exist_ok=True)
    image=[0]*65536;raw=(OUT/'software/image.bin').read_bytes()
    for i in range(0,len(raw),2):image[32768+(s['INIT']+i)//2]=int.from_bytes(raw[i:i+2],'little')
    boot=Program(0o2400).mov(6,0o3700).mov(0,5).emit(0o42)
    boot.mov(0,0o7000).store(0,0o122).emit(0o4737,s['INIT']).store(0,0o6004)
    for addr,target in ((0o6000,s['FPS']),(0o6002,0o102),(0o6010,s['FEC']),(0o6012,s['FEA'])):
        boot.load(0,addr).store(0,target)
    boot.mov(0,0o1000).store(0,0o100)
    for r in range(7):boot.load(r,0o6100+2*r)
    boot.emit(0o10)
    for i,w in enumerate(boot.words):image[32768+boot.base//2+i]=w
    (OUT/'test-image.mem').write_text(''.join(f'{w:04x}\n' for w in image))
    (OUT/'fp70_symbols.vh').write_text(''.join(f'localparam FP_{n}={s[n]};\n' for n in ('FPS','FEC','FEA','ACS','ENTER','MEMEND')))
    cc=['cc','-O2','-Wall','-DENABLE_MMU=0','-I..','tb/reference_fp_transfer_cp70.c','../core/core.c','../core/hardware.c','-lm','-o',str(out/'oracle')]
    subprocess.run(cc,cwd=ROOT,capture_output=True,check=True)
    with (out/'vectors.txt').open('w') as log,(out/'oracle.log').open('w') as err:
        subprocess.run([str(out/'oracle')],cwd=ROOT,stdout=log,stderr=err,check=True)
    # Vendor/logic sampling retains every encoding and every injection kind,
    # plus all one-shot faults and odd-address probes. Preserve complete rows, never truncate a live file.
    if mode!='sync':
        rows=(out/'vectors.txt').read_text().splitlines()
        rows=[row for row in rows if int(row.split()[78],16) in (0,1,2,3,4,5,6,7,18,19,22,23) or int(row.split()[13],16)!=0 or int(row.split()[78],16)&0x8000]
        if mode=='vendor':
            def selected(row):
                v=[int(x,16) for x in row.split()];op,seed=v[0],v[78];spec=op&0o77;ac=(op>>6)&3
                floating=(op&0o177400) in (0o172400,0o174000)
                if not floating:return seed==0 and (spec==0 or spec==7 or (spec&7)==2)
                if v[128]==1:return seed in (0,1,4,5)
                # Every AC x mode x F/D; rotate CPU register across modes.
                if seed in (0,1) and (spec&7)==((spec>>3)&7):return True
                # All memory beat faults + odd addresses for representative R2/R7, AC0.
                return ac==0 and ((spec&7) in (2,7)) and (v[13]!=0 or seed&0x8000)
            rows=[row for row in rows if selected(row)]
        (out/'vectors.txt').write_text('\n'.join(rows)+'\n')
    sources=['tb/tb_fp_transfer_cp70.v']+core
    if mode=='vendor':
        sources+=['build/cp67-modules/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_fp_transfer_cp70','-I'+str(OUT),'-o',str(out/'sim')]+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_transfer_cp70','-GROM_DECODE='+('0' if mode=='logic' else '1'),'--Mdir',str(out/'obj'),'-I'+str(OUT)]+sources
        sim=[str(out/'obj/Vtb_fp_transfer_cp70')]
    paths=sources+['firmware/fp11/FP11.MAC','tb/reference_fp_transfer_cp70.c','tools/test_fp_transfer_cp70.py',
        'build/cp70-fp11/test-image.mem','build/cp70-fp11/fp70_symbols.vh',str((out/'vectors.txt').relative_to(ROOT)),
        '../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c']
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (out/'result.json').unlink(missing_ok=True)
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim+[f'+VECTORS={out}/vectors.txt',f'+METRICS={out}/metrics.csv'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True);r.check_returncode()
    for p,h in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,mode=mode,fp=fp,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mode',choices=['sync','logic','vendor'],default='sync');run(p.parse_args().mode)
