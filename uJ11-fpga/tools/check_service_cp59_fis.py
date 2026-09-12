#!/usr/bin/env python3
"""Unchanged exact FIS oracle on the service-bank CPU, with both ready bits off."""
import hashlib
import json
import argparse
from board_common import ROOT
from build_timing_cp59 import adapt, OUT
from run_fis_tests import compile_test, execute, CORE


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vendor-stride',type=int,default=37,
        help='Every Nth exact vector on Lattice EBRs; 1 runs the complete set')
    args=parser.parse_args()
    if args.vendor_stride<1:parser.error('vendor stride must be positive')
    adapt()
    tb=(ROOT/'tb/tb_fis.v').read_text().replace('ROM_DECODE=0','ROM_DECODE=1')
    tb=tb.replace('.mem_addr(addr)', '.mem_bank(),.mem_physical(),.mem_addr(addr)')
    (OUT/'tb_fis.v').write_text(tb)
    joined=OUT/'core-with-decode.v'
    joined.write_text('\n'.join((OUT/'src'/p).read_text() for p in
        ['rtl/uj11_core.v','rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v']))
    replacements={p:str(OUT/'src'/p) for p in CORE}
    replacements.update({'rtl/uj11_core.v':str(joined),'tb/tb_fis.v':str(OUT/'tb_fis.v'),
        'microcode/generated/uj11_m0_ebr.v':str(OUT/'uj11_m0_ebr.v')})
    vectors='build/fis-vectors.txt'
    fixtures=(ROOT/vectors).read_text().splitlines()
    vendor_vectors='build/cp59-service/fis-vendor-vectors.txt'
    (ROOT/vendor_vectors).write_text('\n'.join(fixtures[::args.vendor_stride])+'\n')
    source_files=['tools/check_service_cp59_fis.py','tools/run_fis_tests.py',vectors,vendor_vectors,'tb/tb_fis.v',
        'build/cp59-service/inputs.json','build/cp59-service/core-with-decode.v','build/cp59-service/tb_fis.v']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in source_files}
    for vendor in (False,True):
        tag='cp59-fis-'+('vendor' if vendor else 'portable')
        cmd=compile_test(-1,'iverilog' if vendor else 'verilator',vendor,candidate=False,tag=tag,replacements=replacements)
        execute(cmd,tag,vendor_vectors if vendor else vectors,jobs=4)
        log=(ROOT/f'build/{tag}.log').read_text();print(log.splitlines()[-1])
    for p,h in hashes.items():assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h,p
    outputs=['build/cp59-fis-portable.log','build/cp59-fis-portable.csv','build/cp59-fis-vendor.log','build/cp59-fis-vendor.csv']
    record=dict(portable_cases=len(fixtures),vendor_cases=len(fixtures[::args.vendor_stride]),vendor_stride=args.vendor_stride,
        inputs=hashes,outputs={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in outputs})
    (OUT/'fis-inputs.json').write_text(json.dumps(record,indent=2)+'\n')

if __name__=='__main__':main()
