#!/usr/bin/env python3
"""Independent C address/control oracles and strict lint for new CP44 units."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    tests=[];sources={'tools/check_relocate_units_cp44.py'}
    for suite in ('relocate','mmr0'):
        oracle='tb/reference_'+suite+'_cp44.c'
        deps=[oracle,'../core/core.c','../core/core.h','../core/pdp11_fp.c','../core/hardware.c','../core/hardware.h']
        with (ROOT/f'build/cp44-{suite}-cc.log').open('w') as log:
            subprocess.run(['cc','-O2','-Wall','-DENABLE_MMU=1','-I..',oracle,'../core/hardware.c','-lm',
                            '-o','build/cp44-'+suite+'-oracle'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        corpus='build/cp44-'+suite+'-oracle.txt'
        with (ROOT/corpus).open('w') as out:
            subprocess.run(['build/cp44-'+suite+'-oracle'],cwd=ROOT,stdout=out,check=True)
        top='tb_'+suite+'_oracle_cp44';tag='cp44-'+suite+'-oracle-test'
        rtl='rtl/experimental/'+('uj11_mmu_relocate' if suite=='relocate' else 'uj11_mmr0_control')+'.v'
        files=['tb/'+top+'.v',rtl]
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog','-g2012','-Wall','-s',top,'-o','build/'+tag]+files,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(['vvp','build/'+tag],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        lines=[s for s in (ROOT/f'build/{tag}.log').read_text().splitlines() if s.startswith('PASS')]
        assert len(lines)==1,lines
        tests.append(dict(tag=tag,pass_line=lines[0],corpus=corpus,corpus_sha256=hashlib.sha256((ROOT/corpus).read_bytes()).hexdigest()))
        sources.update(deps+files+[corpus]);print(lines[0],flush=True)
    with (ROOT/'build/cp44-units-lint.log').open('w') as log:
        for rtl in ('uj11_mmu_relocate','uj11_mmr0_control','uj11_mmu_entry_cp44'):
            path='rtl/experimental/'+rtl+'.v';sources.add(path)
            subprocess.run(['verilator','--lint-only','--Wall','--top-module',rtl,path],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    report=dict(tests=tests,strict_lint_modules=3,inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(sources)})
    (ROOT/'build/cp44-units.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP44 strict lint: three new RTL units')


if __name__=='__main__':main()
