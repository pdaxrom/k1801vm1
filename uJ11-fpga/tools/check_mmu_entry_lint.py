#!/usr/bin/env python3
"""Strict lint of the isolated controller and the two equally observed CPUs."""
import subprocess
from board_common import ROOT
from checkpoint import CONFIGS


def main():
    with (ROOT/'build/cp35-final-lint.log').open('w') as log:
        for name in ('cp35a', 'cp35b', 'cp35c'):
            top, _, sources = CONFIGS[name]
            # Vendor primitives are checked in Icarus and Diamond. Strict
            # logic lint uses the equivalent portable synchronous microstore.
            sources = ['rtl/uj11_rom.v' if s.endswith('/uj11_m0_ebr.v') else s
                       for s in sources]
            subprocess.run(['verilator', '--lint-only', '--Wall', '--top-module', top]+sources,
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        log.write('PASS strict lint: entry, baseline core, selected candidate core\n')


if __name__ == '__main__':
    main()
