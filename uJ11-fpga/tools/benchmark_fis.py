#!/usr/bin/env python3
"""Benchmark guest MOV/MOV/MOV/FIS/BR loops and verify every FIS result."""
import argparse
import subprocess
from fis_reference import reference
from run_fis_tests import ROOT,compile_test


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vendor',action='store_true')
    p.add_argument('--memory',type=int,nargs='+',choices=(-1,1,2),default=[-1,1,2])
    args=p.parse_args()
    inputs=[(0,0x40800000,0x40800000),(1,0x40c00000,0x40800000),
            (2,0x40c00000,0x40c00000),(3,0x40800000,0x40c00000),
            (0,0xc0800001,0x34800001),(1,0x40800001,0x40800000),
            (2,0x00800000,0x41000000),(3,0xc0c00000,0x40a00000)]
    lines=[]
    for op,a,b in inputs:
        answer,flags,error=reference(op,a,b);assert not error
        lines.append(f'{op:x} {a:08x} {b:08x} {answer:08x} {flags:x}')
    (ROOT/'build/fis-bench-vectors.txt').write_text('\n'.join(lines)+'\n')
    for mode in args.memory:
        tag=f'fis-bench-{"vendor" if args.vendor else "portable"}-{mode}'
        binary=compile_test(mode,'iverilog' if args.vendor else 'verilator',args.vendor,
                            tag=tag,top='tb_fis_bench')
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(binary,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        data=(ROOT/f'build/fis-benchmarks-{mode}.json').read_bytes()
        (ROOT/f'build/{tag}.json').write_bytes(data)
        print((ROOT/f'build/{tag}.log').read_text().strip(),flush=True)


if __name__=='__main__':main()
