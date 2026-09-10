#!/usr/bin/env python3
"""Four-state datapath comparison with independently frozen CP39 reference ALU."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp40-alu-sim';out.mkdir(exist_ok=True)
    base='build/cp40-mux/baseline/uj11_datapath.v'
    old_alu='build/cp40-alu/baseline/uj11_alu.v'
    ref=out/'uj11_datapath_reference.v'
    ref.write_text((ROOT/base).read_text().replace('module uj11_datapath (','module uj11_datapath_reference (')
                   .replace('uj11_alu alu(','uj11_alu_reference alu('))
    alu_ref=out/'uj11_alu_reference.v'
    alu_ref.write_text((ROOT/old_alu).read_text().replace('module uj11_alu (','module uj11_alu_reference ('))
    common=['tb/tb_datapath_mux.v','rtl/uj11_regfile.v',str(ref.relative_to(ROOT)),str(alu_ref.relative_to(ROOT))]
    inputs=common+['tools/check_alu_mux_sim.py',base,old_alu];tests=[]
    for dp,alu in [('both','balanced'),('baseline','balanced'),('both','priority')]:
        name=dp+'-'+alu
        sources=common+[f'build/cp40-mux/{dp}/uj11_datapath.v',f'build/cp40-alu/{alu}/uj11_alu.v']
        inputs+=sources
        with (out/(name+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_datapath_mux','-o',str(out/name)]+sources,
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(['vvp',str(out/name)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        line=next(s for s in (out/(name+'.log')).read_text().splitlines() if s.startswith('PASS'))
        tests.append(dict(variant=name,pass_line=line));print(name+': '+line,flush=True)
    (ROOT/'build/cp40-alu-sim.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
