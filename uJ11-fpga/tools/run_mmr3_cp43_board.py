#!/usr/bin/env python3
"""Cold RT-11FB run of the CP43 APR/MMR3 board; images remain read-only."""
import hashlib
import json
import sys
import run_board
from board_common import ROOT
from build_mmr3_cp43 import adapt


def main():
    run_board.CORE,run_board.BOARD=adapt(run_board.CORE,run_board.BOARD)
    tag='cp43'
    extras=['tools/run_mmr3_cp43_board.py','tools/build_mmr3_cp43.py',
            'build/cp43-mmr3/inputs.json','tools/build_mmu_apr_csr.py',
            'build/cp39-csr/inputs.json','build/cp39-csr/lookup.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extras}
    sys.argv=[sys.argv[0],'--tag',tag];run_board.main()
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    report=json.loads((ROOT/f'build/{tag}-board-inputs.json').read_text())
    report['files'].update(hashes)
    report.update(cpu_apr_csr=True,mmr3_csr=True,translation=False,rt11_xm=False)
    (ROOT/f'build/{tag}-verified.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
