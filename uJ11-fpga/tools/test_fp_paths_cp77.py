#!/usr/bin/env python3
"""Differential word/status addressing and fault tests on frozen CP67 hardware."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT
from build_fp11_cp77 import build, OUT
from build_modules_cp67 import adapt, OUT as HW
from run_service_cp57 import Program
from fp_reference_cp76 import prepare


def run(mode='sync',smoke=False):
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
    (OUT/'fp76_symbols.vh').write_text(''.join(f'localparam FP_{n}={s[n]};\n' for n in ('FPS','FEC','FEA','ACS','ENTER','MEMEND','DLOOP','MNORM','MMLOOP','MPINT')))
    reference=out/'reference';adaptation=prepare(reference)
    cc=['cc','-O2','-Wall','-DENABLE_MMU=0','-I..','tb/reference_fp_arith_cp76.c',str(reference/'core.c'),str(reference/'hardware.c'),'-lm','-o',str(out/'oracle')]
    subprocess.run(cc,cwd=ROOT,capture_output=True,check=True)
    with (out/'vectors.txt').open('w') as log,(out/'oracle.log').open('w') as err:
        subprocess.run([str(out/'oracle')],cwd=ROOT,stdout=log,stderr=err,check=True)
    # Logic retains every encoding, all faults and odd probes; vendor is a
    # representative AC/mode/precision/fault sample. Preserve complete rows, never truncate a live file.
    if mode!='sync':
        rows=(out/'vectors.txt').read_text().splitlines()
        if mode=='logic':rows=[row for row in rows if int(row.split()[78],16) in (0,1,2,3,4,5,6,7,18,19,22,23) or int(row.split()[13],16)!=0 or int(row.split()[78],16)>=512]
        if mode=='vendor':
            def selected(row):
                v=[int(x,16) for x in row.split()];op,seed=v[0],v[78]
                spec=op&0o77;ac=(op>>6)&3;family=op&0o177400
                if family>=0o175000:
                    if ac!=2:return False
                    if seed>=512:return spec==5 and (seed>>9) in (1,2,9,17,21,25,26) and (seed&511) in (0,1,3,8,16,64,128,255)
                    return spec in (5,6,0o22,0o27,0o62) and seed in (0,1,2,3,48,49,50,51)
                if family==0o171400:
                    if seed>=512:return ac in (2,3) and spec==5 and (seed>>9) in (1,8,13,17,33,34,37,39,42,45) and (seed&511) in (0,1,57)
                    if ac!=2:return False
                    if spec<8:return spec in (5,6) and seed in ((0,1,4,5) if spec>=6 else (0,1))
                    return (spec&7) in (2,7) and seed in (0,1) and not v[13]
                # Existing transfer controls and the changed memory unary UV
                # paths (including each operand-write failure) on vendor EBRs.
                if op&0o177700 in (0o170600,0o170700):
                    return spec in (0o22,0o27,0o62) and seed in (18,19,22,23)
                return op in (0o170000,0o170100,0o172605,0o174205,0o173605,0o172205,0o173205) and seed in (0,1)
            rows=[row for row in rows if selected(row)]
        (out/'vectors.txt').write_text('\n'.join(rows)+'\n')
    if smoke:
        rows=(out/'vectors.txt').read_text().splitlines()
        rows=[row for row in rows if int(row.split()[0],16) in tuple(base+0o200+sp for base in range(0o175000,0o200000,0o400) for sp in (5,0o22)) and (int(row.split()[78],16)<512 or (int(row.split()[78],16)&511) in (0,1,2,3,4,8,16,32,64,128,255))]
        (out/'vectors.txt').write_text('\n'.join(rows)+'\n')
    sources=['tb/tb_fp_paths_cp77.v']+core
    if mode=='vendor':
        sources+=['build/cp67-modules/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_fp_paths_cp77','-I'+str(OUT),'-o',str(out/'sim')]+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_paths_cp77','-GROM_DECODE='+('0' if mode=='logic' else '1'),'--Mdir',str(out/'obj'),'-I'+str(OUT)]+sources
        sim=[str(out/'obj/Vtb_fp_paths_cp77')]
    paths=sources+['firmware/fp11/FP11.MAC','tb/reference_fp_arith_cp76.c','tb/reference_fp_add_cp73.h','tb/reference_fp_muldiv_cp74.h','tools/fp_reference_cp76.py','tools/test_fp_paths_cp77.py',
        'build/cp77-fp11/test-image.mem','build/cp77-fp11/fp76_symbols.vh',str((out/'vectors.txt').relative_to(ROOT)),
        '../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c']
    paths+=list(adaptation['adapted'])
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (out/'result.json').unlink(missing_ok=True)
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim+[f'+VECTORS={out}/vectors.txt',f'+METRICS={out}/metrics.csv'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True);r.check_returncode()
    for p,h in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,mode=mode,fp=fp,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mode',choices=['sync','logic','vendor'],default='sync');p.add_argument('--smoke',action='store_true');a=p.parse_args();run(a.mode,a.smoke)
