#!/usr/bin/env python3
"""Reproduce the CP27 FIS gate; inherited integer vendor results stay CP26 evidence."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--phase', choices=('all', 'portable', 'vendor', 'record'), default='all')
    args = p.parse_args()
    (ROOT/'build').mkdir(exist_ok=True)

    def run(command, name):
        print('Run:', ' '.join(command), '→', name, flush=True)
        with (ROOT/'build'/name).open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)

    if args.phase in ('all', 'portable'):
        run(['make', 'all'], 'cp27-build.log')
        run(['make', 'test-reference-core', 'test-python'], 'cp27-core-python.log')
        run([sys.executable, 'tools/check_decode_cp27.py'], 'cp27-decode.log')
        run(['make', '-j4', 'test'], 'cp27-integer-regression.log')
        run([sys.executable, 'tools/check_fis_negative.py'], 'cp27-fis-negative.log')
        run([sys.executable, 'tools/check_datapath_cp27.py', '--yosys', os.environ.get('YOSYS', 'yosys')],
            'cp27-datapath-proof.log')
        run([sys.executable, 'tools/benchmark_fis.py'], 'cp27-fis-bench-portable.log')
    if args.phase in ('all', 'vendor'):
        run(['make', 'all', 'build/fis-vectors.txt'], 'cp27-vendor-prepare.log')
        run([sys.executable, 'tools/run_fis_tests.py', '--vendor', '--jobs', '4'], 'cp27-fis-vendor.log')
        run([sys.executable, 'tools/benchmark_fis.py', '--vendor'], 'cp27-fis-bench-vendor.log')
    if args.phase in ('all', 'record'):
        subprocess.run([sys.executable, 'tools/record_cp27.py'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
