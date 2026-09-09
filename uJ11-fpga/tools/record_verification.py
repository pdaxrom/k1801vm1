#!/usr/bin/env python3
"""Archive successful verification logs and hash the exact verified inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tests',type=Path,required=True)
    p.add_argument('--benchmarks',type=Path,required=True)
    a=p.parse_args()
    tests=a.tests.read_text(); bench=a.benchmarks.read_text()
    if 'FATAL' in tests+bench or tests.count('PASS differential: 6272')!=2 or bench.count('PASS benchmarks: 15 runs')!=2:
        p.exit(1,'Expected passing portable and vendor differential/benchmark runs.\n')
    if (ROOT/'build/benchmarks.json').read_bytes()!=(ROOT/'build/benchmarks-portable.json').read_bytes():
        p.exit(1,'Portable and vendor cycle counts differ.\n')
    out=ROOT/'tb/reports'; out.mkdir(exist_ok=True)
    shutil.copyfile(a.tests,out/'m0-tests.log')
    shutil.copyfile(a.benchmarks,out/'m0-benchmarks.log')
    shutil.copyfile(ROOT/'build/benchmarks.json',ROOT/'docs/benchmarks-m0.json')
    inputs=[]
    for directory,pattern in [('rtl','*.v'),('microcode','*.uasm'),('microasm','*.py'),('tb','*.v'),('tb','*.c'),('tb','*.py')]:
        inputs.extend((ROOT/directory).glob(pattern))
    inputs.extend(ROOT/name for name in ['Makefile','tools/make_ebr.py','tools/record_verification.py',
        'microcode/generated/m0.mem','microcode/generated/uj11_m0_ebr.v','build/isa_vectors.mem',
        '../core/core.c','../core/core.h','../core/hardware.c','../core/hardware.h','../core/pdp11_fp.c'])
    hashes={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(inputs)}
    record={'encoding_version':json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())['encoding_version'],
        'oracle':'existing DCJ11 core; compiled with ENABLE_MMU=0','differential_cases_per_rom_model':6272,
        'alu_checks':66592,'decoder_encodings_checked':65536,'sequencer_checks':4323,
        'benchmark_runs_per_rom_model':15,'measured_instructions_per_benchmark':4096,
        'rom_models':['portable synchronous Verilog','Diamond 3.14 DP8KC'],
        'hardware_execution_tested':False,'files':hashes,
        'logs':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in out.glob('m0-*.log')}}
    (ROOT/'docs/verification-m0.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Archived M0 verification logs and input hashes')


if __name__=='__main__': main()
