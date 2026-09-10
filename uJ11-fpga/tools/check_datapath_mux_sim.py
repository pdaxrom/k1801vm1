#!/usr/bin/env python3
"""Icarus four-state checks for all three frozen CP40 datapath candidates."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp40-sim';out.mkdir(exist_ok=True)
    base=ROOT/'build/cp40-mux/baseline/uj11_datapath.v'
    ref=out/'uj11_datapath_reference.v'
    ref.write_text(base.read_text().replace('module uj11_datapath (','module uj11_datapath_reference ('))
    common=['tb/tb_datapath_mux.v','build/cp40-alu/baseline/uj11_alu.v','rtl/uj11_regfile.v',str(ref.relative_to(ROOT))]
    inputs=common+['tools/check_datapath_mux_sim.py',str(base.relative_to(ROOT))]
    tests=[]
    for variant in ('shift','pair','both'):
        path=f'build/cp40-mux/{variant}/uj11_datapath.v';inputs.append(path)
        with (out/(variant+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_datapath_mux','-o',str(out/variant)]+common+[path],
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(variant+'.log')).open('w') as log:
            subprocess.run(['vvp',str(out/variant)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        line=next(s for s in (out/(variant+'.log')).read_text().splitlines() if s.startswith('PASS'))
        tests.append(dict(variant=variant,pass_line=line));print(variant+': '+line,flush=True)
    (ROOT/'build/cp40-sim.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}),indent=2)+'\n')


if __name__=='__main__':main()
