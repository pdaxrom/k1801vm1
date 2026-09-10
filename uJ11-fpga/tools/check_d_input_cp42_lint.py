#!/usr/bin/env python3
"""Strict lint of the adopted D-input and both CP39 core configurations."""
import hashlib
import json
import subprocess
from board_common import ROOT,CORE,BOARD
from build_mmu_apr_csr import adapt


def main():
    sources,_=adapt(CORE,BOARD);sources+=['rtl/uj11_rom.v']
    targets=[('uj11_core',['-GROM_DECODE=0','-GALIGNED_WORD_READS=0']),
             ('uj11_core',['-GROM_DECODE=1','-GALIGNED_WORD_READS=1']),
             ('uj11_datapath',[]),('uj11_engine',[])]
    with (ROOT/'build/cp42-lint.log').open('w') as log:
        for top,flags in targets:
            subprocess.run(['verilator','--lint-only','--Wall','--top-module',top]+flags+sources,
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        log.write('PASS CP42 strict lint: two core configurations, engine, datapath\n')
    sources+=['tools/check_d_input_cp42_lint.py']
    (ROOT/'build/cp42-lint.json').write_text(json.dumps(dict(configurations=4,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}),indent=2)+'\n')
    print('PASS CP42 strict lint: 4 configurations')


if __name__=='__main__':main()
