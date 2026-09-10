#!/usr/bin/env python3
"""Strict CP39 core and shared APR controller lint, without warning waivers."""
import hashlib
import json
import subprocess
from board_common import ROOT,CORE,BOARD
from build_mmu_apr_csr import adapt


def main():
    core,_=adapt(CORE,BOARD)
    sources=core+['rtl/uj11_rom.v']
    targets=[('uj11_core',['-GROM_DECODE=0','-GALIGNED_WORD_READS=0']),
             ('uj11_core',['-GROM_DECODE=1','-GALIGNED_WORD_READS=1']),
             ('uj11_mmu_apr_shared',[])]
    with (ROOT/'build/cp39-lint.log').open('w') as log:
        for top,flags in targets:
            subprocess.run(['verilator','--lint-only','--Wall','--top-module',top]+flags+sources,
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        log.write('PASS strict CP39 lint: default/aligned ROM-decode core and shared APR port\n')
    sources+=['tools/check_mmu_apr_csr_lint.py','tools/build_mmu_apr_csr.py']
    (ROOT/'build/cp39-lint.json').write_text(json.dumps(dict(configurations=len(targets),
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}),indent=2)+'\n')
    print('PASS strict CP39 lint: 3 configurations')


if __name__=='__main__':main()
