#!/usr/bin/env python3
"""Reproduce functional CP28 tests; synthesis cp28m is separately archived."""
import argparse
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--yosys',default='yosys');p.add_argument('--record-only',action='store_true');args=p.parse_args()
    def run(command,name):
        print('Run:', ' '.join(command),flush=True)
        with (ROOT/'build'/name).open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    if not args.record_only:
        run(['make','board','build/ea-vectors.txt','build/trace_bit-vectors.txt','build/bus-fault-vectors.txt','build/fis-vectors.txt'],'cp28-prepare.log')
        run(['make','test-datapath'],'cp28-new-alu-tests.log')
        run(['make','test-core','test-engine','test-irq','test-decode','lint'],'cp28-default-regression.log')
        run([sys.executable,'tools/check_alu_cp28.py','--yosys',args.yosys],'cp28-alu-proof-driver.log')
        run([sys.executable,'tools/check_board_units.py'],'cp28-units.log')
        run([sys.executable,'tools/check_sync_decode.py'],'cp28-sync-driver.log')
        run([sys.executable,'tools/check_sync_decode.py','--suite','fis'],'cp28-sync-fis-driver.log')
        run([sys.executable,'tools/run_board.py'],'cp28-board-driver.log')
    subprocess.run([sys.executable,'tools/record_cp28.py'],cwd=ROOT,check=True)
if __name__=='__main__':main()
