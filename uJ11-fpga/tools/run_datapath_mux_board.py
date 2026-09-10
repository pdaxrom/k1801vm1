#!/usr/bin/env python3
"""Cold FB runs for adopted CP40 RTL, preserving production and APR evidence."""
import argparse
import hashlib
import json
import sys
import run_board
from board_common import ROOT
from build_mmu_apr_csr import adapt


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--apr',action='store_true');args=parser.parse_args()
    if args.apr:run_board.CORE,run_board.BOARD=adapt(run_board.CORE,run_board.BOARD)
    tag='cp40-'+('apr' if args.apr else 'production')
    extras=['tools/run_datapath_mux_board.py','tools/build_mmu_apr_csr.py',
            'build/cp39-csr/inputs.json','build/cp39-csr/lookup.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extras}
    sys.argv=[sys.argv[0],'--tag',tag];run_board.main()
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    report=json.loads((ROOT/f'build/{tag}-board-inputs.json').read_text())
    report['files'].update(hashes);report.update(cpu_apr_csr=args.apr,translation=False,rt11_xm=False)
    (ROOT/f'build/{tag}-verified.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
