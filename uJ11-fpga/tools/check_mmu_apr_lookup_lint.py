#!/usr/bin/env python3
"""Strict logic lint for both supported input-bus configurations of CP37d."""
import hashlib
import json
import subprocess
from board_common import ROOT


def main():
    sources = ['build/cp37-lookup/uj11_core.v', 'build/cp37-lookup/uj11_engine.v',
               'build/cp37-lookup/uj11_microseq.v', 'build/cp37-lookup/uj11_decode_table.v',
               'rtl/uj11_mmu_apr_ram.v', 'rtl/experimental/uj11_mmu_entry.v',
               'rtl/uj11_alu.v', 'rtl/uj11_regfile.v', 'rtl/uj11_datapath.v',
               'rtl/uj11_psw.v', 'rtl/uj11_mem.v', 'rtl/uj11_decode.v',
               'rtl/uj11_decode_rom.v', 'rtl/uj11_rom.v']
    with (ROOT/'build/cp37-lint.log').open('w') as log:
        for mode in (0, 1):
            subprocess.run(['verilator', '--lint-only', '--Wall', '--top-module', 'uj11_core',
                            f'-GROM_DECODE={mode}', f'-GALIGNED_WORD_READS={mode}']+sources,
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        log.write('PASS strict CP37 lint: default and aligned-word ROM-decode core\n')
    sources += ['tools/check_mmu_apr_lookup_lint.py']
    (ROOT/'build/cp37-lint.json').write_text(json.dumps(dict(configurations=2,
        inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}), indent=2)+'\n')


if __name__ == '__main__':
    main()
