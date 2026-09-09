#!/usr/bin/env python3
"""Prove the grouped ALU matches CP27 for every two-state input; reject a mutation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from board_common import ROOT

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--yosys',default='yosys');args=p.parse_args()
    baseline=ROOT/'reference/uj11/alu_cp27.v';candidate=ROOT/'rtl/uj11_alu.v'
    work=ROOT/'build/cp28-alu-proof';work.mkdir(parents=True,exist_ok=True)
    (work/'miter.v').write_text('''module miter(input [15:0] a,b,input [3:0] op,input carry,byte_mode,output mismatch);
wire[15:0]gold,actual;wire[3:0]gf,af;
uj11_alu_cp27 refalu(a,b,op,carry,byte_mode,gold,gf);
uj11_alu newalu(a,b,op,carry,byte_mode,actual,af);
assign mismatch={gold,gf}!={actual,af};endmodule
''')
    records={}
    for negative in (False,True):
        source=candidate.read_text()
        if negative:
            before='(operation[0]&carry)^subtract'
            assert before in source
            source=source.replace(before,'subtract')
        path=work/'candidate.v';path.write_text(source)
        script=f'read_verilog {work.relative_to(ROOT)}/miter.v {baseline.relative_to(ROOT)} {path.relative_to(ROOT)}; prep -top miter -flatten; sat -verify -prove mismatch 0 -show-inputs'
        run=subprocess.run([args.yosys,'-Q','-T','-p',script],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                           env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
        kind='negative' if negative else 'positive'
        (ROOT/f'build/cp28-alu-{kind}.log').write_text(run.stdout)
        assert (run.returncode!=0 and 'proof did fail' in run.stdout) if negative else (run.returncode==0 and 'SUCCESS!' in run.stdout),run.stdout[-2000:]
        records[kind]=dict(returncode=run.returncode,log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest())
    report=dict(baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                method='SAT, all A/B/op/carry/byte inputs unconstrained; dropped ADC/SBC carry rejected',results=records)
    (ROOT/'build/cp28-alu-equivalence.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS grouped ALU: complete SAT equivalence; lost carry rejected. Four-state reset covered by datapath simulation.')
if __name__=='__main__':main()
