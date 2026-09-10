#!/usr/bin/env python3
"""SAT proof of exact D cones compiled in CP42; detect byte/offset/APR defects."""
import argparse
import hashlib
import json
import os
import subprocess
from board_common import ROOT
from build_mmu_entry import change


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--yosys',default='yosys');args=parser.parse_args()
    out=ROOT/'build/cp42-proof';out.mkdir(parents=True,exist_ok=True)
    (out/'miter.v').write_text('''module miter(input [35:0] uword,input [15:0] ir,mdr,psw,apr_data,
input [3:0] a,input byte_instruction,mmu_active,output mismatch);
wire [15:0] gd,ge,dd,de;
d_input_reference gold(uword,ir,mdr,psw,apr_data,a,byte_instruction,mmu_active,gd,ge);
d_input gate(uword,ir,mdr,psw,apr_data,a,byte_instruction,mmu_active,dd,de);
assign mismatch={gd,ge}!={dd,de};endmodule
''')
    script='read_verilog miter.v gold.v gate.v; prep -top miter -flatten; sat -verify -prove mismatch 0 -show-inputs'
    (out/'proof.ys').write_text(script+'\n')
    tests=[];inputs=['tools/check_d_input_cp42.py','build/cp42-input/inputs.json']
    for kind,profile in [('tree','production'),('sliced','production'),('tree','apr'),('sliced','apr'),
                         ('bad-sp-step','production'),('bad-sob','production'),('bad-apr','apr')]:
        negative=kind.startswith('bad-');variant='tree' if negative else kind
        base=f'build/cp42-input/baseline/{profile}/d_input.v';path=f'build/cp42-input/{variant}/{profile}/d_input.v';inputs.extend([base,path])
        (out/'gold.v').write_text((ROOT/base).read_text().replace('module d_input(','module d_input_reference('))
        source=(ROOT/path).read_text()
        if kind=='bad-sp-step':source=change(source,"a<4'd6","a<4'd7")
        if kind=='bad-sob':source=source.replace('!ir[14]',"1'b1")
        if kind=='bad-apr':source=change(source,'mmu_active && uword[12:10]==0','mmu_active')
        (out/'gate.v').write_text(source)
        run=subprocess.run([args.yosys,'-s','proof.ys'],cwd=out,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            env=dict(os.environ,YOWASP_CACHE_DIR=str(ROOT/'build/yowasp-cache')))
        tag=kind+'-'+profile;(out/(tag+'.log')).write_text(run.stdout)
        assert (run.returncode!=0 and 'proof did fail' in run.stdout) if negative else (run.returncode==0 and 'SUCCESS!' in run.stdout),run.stdout[-2000:]
        tests.append(dict(tag=tag,negative=negative,returncode=run.returncode,candidate_sha256=hashlib.sha256(source.encode()).hexdigest(),log_sha256=hashlib.sha256(run.stdout.encode()).hexdigest()))
        print('PASS CP42 '+('reject ' if negative else 'equivalence ')+tag,flush=True)
    (ROOT/'build/cp42-proof.json').write_text(json.dumps(dict(method=script,tests=tests,
        assumptions='None on D selectors, full uword/IR, register index, byte, MDR/PSW, APR value or context-active',
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
