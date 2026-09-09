#!/usr/bin/env python3
"""CP30 real FP control microcode, shared-state EBR and independent C oracle."""
import argparse
import subprocess
from board_common import ROOT,CORE

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor',action='store_true');args=p.parse_args()
    tag='cp30-fp-'+('vendor' if args.vendor else 'portable')
    lib=[str(ROOT/'build/vendor'/f'{name}.v') for name in ('DP8KC','GSR','PUR')] if args.vendor else []
    flags=['-DUJ11_VENDOR_ROM'] if args.vendor else []
    for top,sources in [
        ('tb_fp_state',['rtl/uj11_decode.v','rtl/uj11_decode_rom.v','microcode/generated/uj11_decode_table.v']),
        ('tb_fp_control',CORE+['tb/uj11_ram.v', 'microcode/generated/uj11_m0_ebr.v' if args.vendor else 'rtl/uj11_rom.v']),
        ('tb_fp_events',CORE+['tb/uj11_ram.v', 'microcode/generated/uj11_m0_ebr.v' if args.vendor else 'rtl/uj11_rom.v'])]:
        executable=ROOT/'build'/f'{tag}-{top}'
        subprocess.run(['iverilog','-g2012','-s',top,*flags,'-o',str(executable),f'tb/{top}.v',*sources,*lib],cwd=ROOT,check=True)
        log=executable.with_suffix('.log')
        with log.open('w') as out:
            subprocess.run(['vvp',str(executable)],cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,check=True)
        print(log.read_text(),flush=True)
if __name__=='__main__':main()
