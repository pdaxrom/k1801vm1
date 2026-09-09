#!/usr/bin/env python3
"""Run the unchanged exact FIS oracle with the new FP control/state path enabled."""
import argparse
from board_common import ROOT
from run_fis_tests import compile_test,execute

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mode',type=int,choices=(-1,1),default=-1);args=p.parse_args()
    tag=f'cp30-fis-fp-{args.mode}'
    joined=ROOT/f'build/{tag}-core.v'
    joined.write_text('\n'.join((ROOT/s).read_text() for s in
        ['rtl/uj11_core.v','rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v']))
    original=(ROOT/'tb/tb_fis.v').read_text()
    needle='uj11_core #(.ROM_DECODE(ROM_DECODE))'
    assert original.count(needle)==1
    variant=original.replace('ROM_DECODE=0','ROM_DECODE=1',1).replace(needle,'uj11_core #(.ROM_DECODE(ROM_DECODE),.FP11_CONTROL(1))')
    tb=ROOT/f'build/{tag}-tb.v';tb.write_text(variant)
    command=compile_test(args.mode,'verilator',False,tag=tag,replacements={'rtl/uj11_core.v':str(joined),'tb/tb_fis.v':str(tb)})
    execute(command,tag,'build/fis-vectors.txt')
    print((ROOT/'build'/f'{tag}.log').read_text(),flush=True)
if __name__=='__main__':main()
