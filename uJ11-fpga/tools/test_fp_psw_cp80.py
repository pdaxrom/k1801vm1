#!/usr/bin/env python3
"""Manual DEC PSW operand/commit fixtures on unchanged CP67 core and EBRs.

No PSW bus adapter and no internal RTL state injection. The guest executes
ordinary instructions; any external operand access to 177776 is a failure.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from board_common import ROOT, CORE
from build_fp11_cp80 import OUT, build
from build_modules_cp67 import OUT as HW, adapt
from run_service_cp57 import Program


def run(mode='sync'):
    fp=build();adapt();s=fp['symbols'];out=OUT/('psw-'+mode);out.mkdir(exist_ok=True)
    image=[0]*65536;raw=(OUT/'software/image.bin').read_bytes()
    for i in range(0,len(raw),2):image[32768+(s['INIT']+i)//2]=int.from_bytes(raw[i:i+2],'little')
    boot=Program(0o2400).mov(6,0o3700).mov(0,5).emit(0o42)
    boot.mov(0,0o7000).store(0,0o122).emit(0o4737,s['INIT']).store(0,0o6004)
    for addr,target in ((0o6000,s['FPS']),(0o6002,0o102),(0o6006,0o100),(0o6010,s['FEC']),(0o6012,s['FEA'])):
        boot.load(0,addr).store(0,target)
    for r in range(7):boot.load(r,0o6100+2*r)
    boot.emit(0o10)
    for i,w in enumerate(boot.words):image[32768+boot.base//2+i]=w
    (out/'image.mem').write_text(''.join(f'{w:04x}\n' for w in image))
    (out/'fp80_symbols.vh').write_text(''.join(f'localparam FP_{n}={v};\n' for n,v in s.items()))
    header=(ROOT/'tb/tb_fp11_cp68.v').read_text().split('    initial begin\n')[0]
    header=header.replace('module tb_fp11_cp68 #','module tb_fp_psw_cp80 #').replace('fp68_symbols.vh','fp80_symbols.vh')
    header=header.replace('cycles>50000','cycles>200000')
    header=header.replace('    reg [15:0] values[0:22];','    reg [15:0] values[0:22];\n    reg [15:0] guest_start=16\'o1000;')
    header=header.replace("            memory[16'o1000/2]=values[0];", "            memory[32768+16'o6006/2]=guest_start;\n            memory[guest_start>>1]=values[0];")
    for offset in (0,2,4):
        header=header.replace(f"16'o{0o1000+offset:o}/2", f"(16'(guest_start+{offset})>>1)")
    header=header.replace("=16'o1002;","=guest_start+2;")
    header=header.replace('            @(negedge clk);reset=0;', "            memory[16'o177776/2]=16'hdead;\n            @(negedge clk);reset=0;")
    cases=(ROOT/'tb/fp_psw_cp80.vh').read_text().replace('PSW_IMAGE',str((out/'image.mem').relative_to(ROOT)))
    tb=out/'tb.v';tb.write_text(header+cases)
    sources=[str(tb.relative_to(ROOT))]+[str((HW/'src'/p).relative_to(ROOT)) for p in CORE]
    if mode=='vendor':
        sources += [str((HW/'uj11_m0_ebr.v').relative_to(ROOT))]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-DUJ11_VENDOR_ROM','-s','tb_fp_psw_cp80','-I'+str(out),'-o',str(out/'sim')]+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_psw_cp80',
             '-GROM_DECODE='+('0' if mode=='logic' else '1'),'--Mdir',str(out/'obj'),'-I'+str(out)]+sources
        sim=[str(out/'obj/Vtb_fp_psw_cp80')]
    paths=[ROOT/p for p in sources]+[ROOT/'tb/tb_fp11_cp68.v',ROOT/'tb/fp_psw_cp80.vh',Path(__file__),
        ROOT/'firmware/fp11/FP11.MAC',out/'image.mem',out/'fp80_symbols.vh']
    inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    (out/'result.json').unlink(missing_ok=True)
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:],flush=True);r.check_returncode()
    counts=re.search(r'PASS CP80 PSW: (\d+) cases / (\d+) checks',(out/'simulation.log').read_text());assert counts
    for p,digest in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,mode=mode,cases=int(counts[1]),checks=int(counts[2]),fp=fp,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mode',choices=('sync','logic','vendor'),default='sync')
    run(p.parse_args().mode)
