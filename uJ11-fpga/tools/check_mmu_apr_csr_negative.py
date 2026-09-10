#!/usr/bin/env python3
"""Prove CP39 tests reject shared-port and physical-DMA regressions."""
import hashlib
import json
import subprocess
from board_common import ROOT,BOARD
from build_mmu_apr_csr import adapt
from build_mmu_entry import change


def main():
    out=ROOT/'build/cp39-negative';out.mkdir(exist_ok=True)
    shared='build/cp39-csr/uj11_mmu_apr_shared.v'
    bus='build/cp39-csr/uj11_board_bus.v'
    defects=[('lookup-kind',shared,'lookup_address : address',"(lookup_address ^ 7'd1) : address",'stale/wrong entry'),
             ('collision',shared,'lookup_request && !request && !busy && !reset','lookup_request && !reset','lookup stole'),
             ('w-clear',shared,'mark_written && !csr_write','mark_written','shared PDR stale/wrong entry'),
             ('physical-dma',bus,'apr_selected=cpu_io_page','apr_selected=io_page','DMA intercepted')]
    _,board=adapt([],BOARD)
    results=[];inputs={'tools/check_mmu_apr_csr_negative.py','tools/check_mmu_apr_csr.py'}
    for name,path,before,after,message in defects:
        target=out/(name+'.v');target.write_text(change((ROOT/path).read_text(),before,after))
        if path==shared:
            top='tb_mmu_apr'
            sources=['build/cp39-tests/tb_mmu_apr.v',shared,'rtl/uj11_mmu_apr_ram.v']
            flags=['-Ptb_mmu_apr.FULL=0']
        else:
            top='tb_board_bus';flags=[]
            sources=['build/cp39-tests/tb_board_bus.v']+board+[shared,'rtl/uj11_mmu_apr_ram.v',
                'rtl/uj11_mmu_apr_decode.v','reference/lsi11/spi_fram_model.v']
        inputs.update(sources);inputs.add(str(target.relative_to(ROOT)))
        compiled=[str(target.relative_to(ROOT)) if p==path else p for p in sources]
        with (out/(name+'-build.log')).open('w') as log:
            subprocess.run(['iverilog','-g2012','-s',top,'-o',str(out/name)]+flags+compiled,
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        run=subprocess.run(['vvp',str(out/name)],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(name+'.log')).write_text(run.stdout)
        assert run.returncode!=0 and 'FATAL:' in run.stdout and message in run.stdout,(name,run.stdout)
        results.append(dict(defect=name,returncode=run.returncode,diagnostic=run.stdout.strip()))
        print('PASS rejected '+name,flush=True)
    (ROOT/'build/cp39-negative.json').write_text(json.dumps(dict(tests=results,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(inputs)}),indent=2)+'\n')


if __name__=='__main__':main()
