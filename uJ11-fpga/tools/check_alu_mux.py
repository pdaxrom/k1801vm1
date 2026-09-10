#!/usr/bin/env python3
"""SAT proof of CP40 ALU results/NZVC; reject lost carry and swapped shifts."""
import argparse
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--yosys',default='yosys');args=parser.parse_args()
    out=ROOT/'build/cp40-alu-proof';out.mkdir(exist_ok=True)
    base='build/cp40-alu/baseline/uj11_alu.v'
    (out/'gold.v').write_text((ROOT/base).read_text().replace('module uj11_alu (','module uj11_alu_reference ('))
    (out/'miter.v').write_text('''module miter(input [15:0] a,b,input [3:0] op,input carry,byte_mode,output mismatch);
wire [15:0] expected,actual;wire [3:0] ef,af;
uj11_alu_reference gold(a,b,op,carry,byte_mode,expected,ef);
uj11_alu gate(a,b,op,carry,byte_mode,actual,af);
assign mismatch={expected,ef}!={actual,af};endmodule
''')
    script='read_verilog miter.v gold.v gate.v; prep -top miter -flatten; sat -verify -prove mismatch 0 -show-inputs'
    (out/'proof.ys').write_text(script+'\n')
    tests=[];inputs=[base,'build/cp40-alu/inputs.json','tools/check_alu_mux.py']
    for kind in ('balanced','priority','lost-carry','swapped-shifts'):
        variant=kind if kind in ('balanced','priority') else 'balanced'
        path=f'build/cp40-alu/{variant}/uj11_alu.v';inputs.append(path)
        source=(ROOT/path).read_text()
        if kind=='lost-carry':source=change(source,'(operation[0]&carry)^subtract','subtract')
        if kind=='swapped-shifts':source=change(source,'right_value : left_value','left_value : right_value')
        (out/'gate.v').write_text(source)
        run=subprocess.run([args.yosys,'-s','proof.ys'],cwd=out,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
        (out/(kind+'.log')).write_text(run.stdout)
        negative=kind!=variant
        assert (run.returncode!=0 and 'proof did fail' in run.stdout) if negative else (run.returncode==0 and 'SUCCESS!' in run.stdout),run.stdout[-2400:]
        tests.append(dict(kind=kind,negative=negative,returncode=run.returncode,
                          candidate_sha256=hashlib.sha256(source.encode()).hexdigest(),log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest()))
        print('PASS ALU '+('reject ' if negative else 'equivalence ')+kind,flush=True)
    (ROOT/'build/cp40-alu-proof.json').write_text(json.dumps(dict(method=script,tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}),indent=2)+'\n')


if __name__=='__main__':main()
