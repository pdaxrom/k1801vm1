#!/usr/bin/env python3
"""Reject broken CP49 state transitions using the SPI pin/data miter."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    out=ROOT/'build/cp49-negative';out.mkdir(exist_ok=True)
    inputs=['tools/check_fram_binary_cp49_negative.py','tb/tb_fram_miter_cp47.v',
            'build/cp49-binary/baseline/uj11_board_fram.v','tools/build_fram_binary_cp49.py','build/cp49-binary/inputs.json']
    gold='build/cp49-negative/uj11_fram_gold.v'
    (ROOT/gold).write_text((ROOT/inputs[2]).read_text().replace('module uj11_board_fram','module uj11_fram_gold'))
    inputs.append(gold);tests=[]
    for variant,defect,old,new,message in [
        ('original','skip-low','LOW:state<=DATA_LO;','LOW:state<=DATA_HI;','SPI/handshake mismatch'),
        ('split-low','byte-skip','state==DATA_LO && byte_access','state==DATA_LO && !byte_access','SPI/handshake mismatch'),
        ('equations','byte-skip','state==DATA_LO && byte_access','state==DATA_LO && !byte_access','SPI/handshake mismatch')]:
        source=f'build/cp49-binary/{variant}/uj11_board_fram.v';inputs.append(source)
        text=(ROOT/source).read_text();assert text.count(old)==1
        mutated='build/cp49-negative/'+variant+'-'+defect+'.v';(ROOT/mutated).write_text(text.replace(old,new));inputs.append(mutated)
        tag='cp49-negative-'+variant+'-'+defect
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s','tb_fram_miter_cp47','-o','build/'+tag,
                'tb/tb_fram_miter_cp47.v',gold,mutated],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        assert not (ROOT/f'build/{tag}-build.log').read_text()
        with (ROOT/f'build/{tag}.log').open('w') as log:
            run=subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        text=(ROOT/f'build/{tag}.log').read_text();assert run.returncode!=0 and message in text,text
        tests.append(dict(tag=tag,defect=defect,returncode=run.returncode,detected=message))
        print('PASS CP49 mutation rejected:',defect,flush=True)
    (ROOT/'build/cp49-negative.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
