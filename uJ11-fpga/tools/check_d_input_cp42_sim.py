#!/usr/bin/env python3
"""Compare CP42 D cones with frozen CP40, including unknown operand data."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp42-sim';out.mkdir(parents=True,exist_ok=True)
    inputs=['tools/check_d_input_cp42_sim.py','tb/tb_d_input_cp42.v','build/cp42-input/inputs.json'];tests=[]
    for kind in ('tree','sliced'):
        for profile in ('production','apr'):
            base=f'build/cp42-input/baseline/{profile}/d_input.v';path=f'build/cp42-input/{kind}/{profile}/d_input.v';inputs.extend([base,path])
            (out/'gold.v').write_text((ROOT/base).read_text().replace('module d_input(','module d_input_reference('))
            tag=kind+'-'+profile
            build=subprocess.run(['iverilog','-g2012','-s','tb_d_input_cp42','-o',str(out/'sim'),
                str(ROOT/'tb/tb_d_input_cp42.v'),str(out/'gold.v'),str(ROOT/path)],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(tag+'-build.log')).write_text(build.stdout);assert build.returncode==0,build.stdout
            run=subprocess.run(['vvp',str(out/'sim')],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            (out/(tag+'.log')).write_text(run.stdout)
            lines=[line for line in run.stdout.splitlines() if line.startswith('PASS CP42')]
            assert run.returncode==0 and len(lines)==1,run.stdout
            tests.append(dict(tag=tag,pass_line=lines[0],log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest()))
            print(lines[0],tag,flush=True)
    (ROOT/'build/cp42-sim.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
