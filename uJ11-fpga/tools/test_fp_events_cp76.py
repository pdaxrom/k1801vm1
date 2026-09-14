#!/usr/bin/env python3
"""Directed FP11 service faults/IRQ/trace/debug tests using the core harness."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT, CORE
from build_fp11_cp76 import OUT
from build_modules_cp67 import OUT as HARDWARE


def run(vendor=False):
    out=OUT/('events-vendor' if vendor else 'events-sync');out.mkdir(exist_ok=True)
    source=(ROOT/'tb/tb_fp11_cp68.v').read_text()
    assert source.count('    initial begin\n')==1
    source=source.split('    initial begin\n')[0].replace('module tb_fp11_cp68 #','module tb_fp_events_cp76 #')
    source=source.replace('build/cp68-fp11','build/cp76-fp11').replace('fp68_symbols.vh','fp76_symbols.vh')
    source=source.replace('cycles>50000','cycles>200000')
    source+='`include "fp_events_cp76.vh"\n'
    tb=out/'tb.v';tb.write_text(source)
    sources=[str(tb.relative_to(ROOT))]+[str((HARDWARE/'src'/p).relative_to(ROOT)) for p in CORE]
    flags=[]
    if vendor:
        flags=['-DUJ11_VENDOR_ROM']
        sources += [str((HARDWARE/'uj11_m0_ebr.v').relative_to(ROOT))]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_fp_events_cp76','-I'+str(OUT),'-Itb','-o',str(out/'sim')]+flags+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_events_cp76',
             '--Mdir',str(out/'obj'),'-I'+str(OUT),'-Itb']+sources
        sim=[str(out/'obj/Vtb_fp_events_cp76')]
    files=sources+['tb/tb_fp11_cp68.v','tb/fp_events_cp76.vh','tools/test_fp_events_cp76.py',
                   'build/cp76-fp11/test-image.mem','build/cp76-fp11/fp76_symbols.vh']
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True);r.check_returncode()
    for p,digest in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,vendor=vendor,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor',action='store_true')
    run(p.parse_args().vendor)
