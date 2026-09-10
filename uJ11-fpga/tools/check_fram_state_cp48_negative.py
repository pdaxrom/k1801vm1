#!/usr/bin/env python3
"""Reject broken CP48 state transitions using the SPI pin/data miter."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp48-negative';out.mkdir(exist_ok=True)
    inputs=['tools/check_fram_state_cp48_negative.py','tb/tb_fram_miter_cp47.v',
            'build/cp48-state/baseline/uj11_board_fram.v','tools/build_fram_state_cp48.py','build/cp48-state/inputs.json']
    gold='build/cp48-negative/uj11_fram_gold.v'
    (ROOT/gold).write_text((ROOT/inputs[2]).read_text().replace('module uj11_board_fram','module uj11_fram_gold'))
    inputs.append(gold);tests=[]
    for variant,defect,old,new,message in [
        ('successors','skip-low','LOW:state<=DATA_LO;','LOW:state<=DATA_HI;','SPI/handshake mismatch'),
        ('onehot','skip-state',"{state[8:0],1'b0}","{state[7:0],2'b0}",'SPI/handshake mismatch')]:
        source=f'build/cp48-state/{variant}/uj11_board_fram.v';inputs.append(source)
        text=(ROOT/source).read_text();assert text.count(old)==1
        mutated='build/cp48-negative/'+defect+'.v';(ROOT/mutated).write_text(text.replace(old,new));inputs.append(mutated)
        tag='cp48-negative-'+defect
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_fram_miter_cp47','-o','build/'+tag,
                'tb/tb_fram_miter_cp47.v',gold,mutated],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        assert not (ROOT/f'build/{tag}-build.log').read_text()
        with (ROOT/f'build/{tag}.log').open('w') as log:
            run=subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        text=(ROOT/f'build/{tag}.log').read_text();assert run.returncode!=0 and message in text,text
        tests.append(dict(tag=tag,defect=defect,returncode=run.returncode,detected=message))
        print('PASS CP48 mutation rejected:',defect,flush=True)
    (ROOT/'build/cp48-negative.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
