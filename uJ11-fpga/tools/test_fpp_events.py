#!/usr/bin/env python3
"""Directed FP11 service faults/IRQ/trace/debug tests using the core harness."""
import argparse
import hashlib
import json
import subprocess
from board_common import ROOT, CORE
from fpp_fixture import prepare
from build_fpp import OUT
from build_hardware import OUT as HARDWARE


def run(vendor=False):
    prepare()
    out=OUT/('events-vendor' if vendor else 'events-sync');out.mkdir(exist_ok=True)
    source=(ROOT/'tests/fpp_harness.vh').read_text()
    source=source.replace('module tb_fpp_harness #','module tb_fpp_events #')
    source=source.replace('fpp_base_symbols.vh','fpp_symbols.vh')
    source=source.replace('cycles>50000','cycles>200000')
    source+=(ROOT/'tests/fpp_events.vh').read_text()
    tb=out/'tb.v';tb.write_text(source)
    sources=[str(tb.relative_to(ROOT))]+CORE.copy()
    flags=[]
    if vendor:
        flags=['-DUJ11_VENDOR_ROM']
        sources += [str((HARDWARE/'uj11_m0_ebr.v').relative_to(ROOT))]+['.cache/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_fpp_events','-I'+str(OUT),'-Itests','-o',str(out/'sim')]+flags+sources
        sim=['vvp',str(out/'sim')]
    else:
        sources+=['rtl/uj11_rom.v']
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-j','4','--top-module','tb_fpp_events',
             '--Mdir',str(out/'obj'),'-I'+str(OUT),'-Itests']+sources
        sim=[str(out/'obj/Vtb_fpp_events')]
    files=sources+['tests/fpp_harness.vh','tests/fpp_events.vh','tools/test_fpp_events.py',
                   'build/fpp/test-image.mem','build/fpp/fpp_symbols.vh']
    inputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-2000:],flush=True);r.check_returncode()
    for p,digest in inputs.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest,p
    (out/'result.json').write_text(json.dumps(dict(passed=True,vendor=vendor,inputs=inputs),indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor',action='store_true')
    run(p.parse_args().vendor)
