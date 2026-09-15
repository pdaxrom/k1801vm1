#!/usr/bin/env python3
"""Directed FP11 service faults/IRQ/trace/debug tests using the core harness."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT, CORE
from build_fp11_cp77 import OUT
from build_psw_cp78 import adapt, OUT as HARDWARE


def run(vendor=False,factored=False):
    if factored:
        from build_psw_cp78b import adapt as build_hw,OUT as hardware
    else:build_hw,hardware=adapt,HARDWARE
    build_hw()
    out=ROOT/('build/cp78b-fp-events' if factored else 'build/cp78-fp-events')/('vendor' if vendor else 'sync');out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'tb/tb_fp11_cp68.v').read_text()
    assert source.count('    initial begin\n')==1
    source=source.split('    initial begin\n')[0].replace('module tb_fp11_cp68 #','module tb_fp_events_cp76 #')
    source=source.replace('build/cp68-fp11','build/cp77-fp11').replace('fp68_symbols.vh','fp76_symbols.vh')
    source=source.replace('cycles>50000','cycles>200000')
    cases=(ROOT/'tb/fp_events_cp76.vh').read_text().replace('build/cp76-fp11','build/cp77-fp11')
    # CP78 fixes J11 reserved PS<10:9> to zero on all loads, including START.
    assert cases.count("16'hff00")==2
    source+=cases.replace("16'hff00","16'hf900")
    tb=out/'tb.v';tb.write_text(source)
    sources=[str(tb.relative_to(ROOT))]+[str((hardware/'src'/p).relative_to(ROOT)) for p in CORE]
    flags=[]
    if vendor:
        flags=['-DUJ11_VENDOR_ROM']
        sources += [str((hardware/'uj11_m0_ebr.v').relative_to(ROOT))]+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_fp_events_cp76','-I'+str(OUT),'-Itb','-o',str(out/'sim')]+flags+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fp_events_cp76',
             '--Mdir',str(out/'obj'),'-I'+str(OUT),'-Itb']+sources
        sim=[str(out/'obj/Vtb_fp_events_cp76')]
    files=sources+['tb/tb_fp11_cp68.v','tb/fp_events_cp76.vh','tools/test_fp_events_cp78.py',str((hardware/'inputs.json').relative_to(ROOT)),str((hardware/'m0.mem').relative_to(ROOT)),str((hardware/'decode.mem').relative_to(ROOT)),
                   'build/cp77-fp11/test-image.mem','build/cp77-fp11/fp76_symbols.vh']
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True);r.check_returncode()
    for p,digest in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,vendor=vendor,factored=factored,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--factored',action='store_true');p.add_argument('--vendor',action='store_true')
    a=p.parse_args();run(a.vendor,a.factored)
