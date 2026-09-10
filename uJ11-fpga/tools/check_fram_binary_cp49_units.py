#!/usr/bin/env python3
"""CP49 protocol/memory scoreboard, four-state miter, reset sweep and lint."""
import hashlib
import json
import subprocess
from board_common import ROOT
from build_fram_binary_cp49 import VARIANTS


def main():
    out=ROOT/'build/cp49-tests';out.mkdir(exist_ok=True)
    baseline='build/cp49-binary/baseline/uj11_board_fram.v'
    gold='build/cp49-tests/uj11_fram_gold.v'
    (ROOT/gold).write_text((ROOT/baseline).read_text().replace('module uj11_board_fram','module uj11_fram_gold'))
    inputs=['tools/check_fram_binary_cp49_units.py','tools/build_fram_binary_cp49.py','build/cp49-binary/inputs.json',baseline,gold,
            'tb/tb_fram_miter_cp47.v','tb/tb_board_fram.v','reference/lsi11/spi_fram_model.v']
    tests=[]
    for variant in VARIANTS[1:]:
        rtl=f'build/cp49-binary/{variant}/uj11_board_fram.v';inputs.append(rtl)
        for divider in (1,2,3):
            for suite in ('miter','memory'):
                top='tb_fram_miter_cp47' if suite=='miter' else 'tb_board_fram'
                files=['tb/'+top+'.v',rtl]+([gold] if suite=='miter' else ['reference/lsi11/spi_fram_model.v'])
                tag=f'cp49-{variant}-{suite}-{divider}'
                params=['-P'+top+'.CLK_DIV='+str(divider)]
                if suite=='miter':params+=['-P'+top+'.QUALIFIED_DATA='+'0']
                with (ROOT/f'build/{tag}-build.log').open('w') as log:
                    subprocess.run(['iverilog','-g2012','-Wall','-s',top,'-o','build/'+tag]+params+files,
                        cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
                assert not (ROOT/f'build/{tag}-build.log').read_text()
                with (ROOT/f'build/{tag}.log').open('w') as log:
                    subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
                lines=[s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS')]
                assert len(lines)==1,lines
                tests.append(dict(tag=tag,variant=variant,divider=divider,suite=suite,pass_line=lines[0]))
                print(lines[0],flush=True)
        with (ROOT/f'build/cp49-{variant}-lint.log').open('w') as log:
            subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_board_fram',rtl],
                cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    (ROOT/'build/cp49-units.json').write_text(json.dumps(dict(tests=tests,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(set(inputs))}),indent=2)+'\n')


if __name__=='__main__':main()
