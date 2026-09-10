#!/usr/bin/env python3
"""Strict CP36 core lint, including enabled aligned-word reads and CP35 hooks."""
import hashlib
import json
import subprocess
from board_common import ROOT
from checkpoint import CONFIGS


def main():
    paths={'tools/check_decode_lint.py','tools/checkpoint.py'}
    with (ROOT/'build/cp36-lint.log').open('w') as log:
        for config in ('cp35b','cp35c'):
            sources=['rtl/uj11_rom.v' if p.endswith('/uj11_m0_ebr.v') else p for p in CONFIGS[config][2]]
            paths.update(sources)
            for aligned in (0,1):
                subprocess.run(['verilator','--lint-only','--Wall','--top-module','uj11_core',
                                '-GROM_DECODE=1',f'-GALIGNED_WORD_READS={aligned}']+sources,
                               cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        log.write('PASS strict CP36 core lint: default/word bus, baseline/context\n')
    (ROOT/'build/cp36-lint.json').write_text(json.dumps(dict(
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(paths)}),indent=2)+'\n')


if __name__=='__main__':
    main()
