#!/usr/bin/env python3
"""Reproduce CP15 portable/vendor verification and archive exact evidence."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUITES = ['ea', 'single', 'branch', 'byte', 'single_byte', 'control', 'extra', 'trap', 'irq', 'illegal']
BENCH = ['benchmark', 'benchmark-fram', 'benchmark-ea', 'benchmark-cp9',
         'benchmark-byte', 'benchmark-control', 'benchmark-extra', 'benchmark-trap', 'benchmark-irq', 'benchmark-illegal']
VENDOR_BASE = ['vendor-test', 'vendor-engine', 'vendor-memory-engine', 'vendor-core', 'vendor-fram']
VENDOR_ISA = ['vendor-ea', 'vendor-single', 'vendor-branch', 'vendor-byte', 'vendor-single-byte',
              'vendor-control', 'vendor-control-faults', 'vendor-extra', 'vendor-extra-faults', 'vendor-trap', 'vendor-trap-faults', 'vendor-irq', 'vendor-irq-faults', 'vendor-irq-lsi11', 'vendor-illegal', 'vendor-illegal-faults']
VENDOR_BENCH = ['vendor-benchmark', 'vendor-fram-benchmark', 'vendor-ea-benchmark',
                'vendor-cp9-benchmark', 'vendor-byte-benchmark', 'vendor-control-benchmark',
                'vendor-extra-benchmark', 'vendor-trap-benchmark', 'vendor-irq-benchmark', 'vendor-illegal-benchmark']
STEMS = ['benchmarks'] + [f'fram-benchmarks-{m}' for m in range(3)] + [
    f'{s}-benchmarks-{m}' for s in ['ea', 'cp9', 'byte', 'single-byte', 'control', 'extra', 'trap', 'irq', 'illegal']
    for m in [-1, 0, 1, 2]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true', help='Show steps; create or run nothing')
    args = parser.parse_args()
    env = dict(os.environ)
    if 'LATTICE_SIM_DIR' not in env and (ROOT/'build/vendor/DP8KC.v').exists():
        env['LATTICE_SIM_DIR'] = str(ROOT/'build/vendor')

    def run(command, log=None):
        print('Run:', ' '.join(command), f'→ build/{log}' if log else '', flush=True)
        if args.dry_run:
            return
        if log:
            with (ROOT/'build'/log).open('w') as output:
                subprocess.run(command, cwd=ROOT, env=env, stdout=output,
                               stderr=subprocess.STDOUT, check=True)
        else:
            subprocess.run(command, cwd=ROOT, env=env, check=True)

    def snapshot(names):
        print(f'Save {len(names)} portable result files before vendor runs', flush=True)
        if not args.dry_run:
            for name in names:
                path = ROOT/'build'/name
                shutil.copyfile(path, path.with_stem(path.stem+'-portable'))

    if not args.dry_run:
        (ROOT/'build').mkdir(exist_ok=True)
    run(['make', 'all'])
    run(['make', 'test'], 'cp15-tests.log')
    snapshot([f'{s}-cycles-{m}.csv' for s in SUITES for m in [-1, 2]])
    run(['make']+BENCH, 'cp15-benchmarks-portable.log')
    snapshot([s+'.json' for s in STEMS])
    run(['make']+VENDOR_BASE, 'cp15-vendor-base.log')
    run(['make']+VENDOR_ISA, 'cp15-vendor-isa.log')
    run(['make']+VENDOR_BENCH, 'cp15-benchmarks-vendor.log')
    run([sys.executable, 'tools/check_decode_equivalence.py'], 'cp15-decode-equivalence.log')
    run([sys.executable, 'tools/record_cp15.py'])


if __name__ == '__main__':
    main()
