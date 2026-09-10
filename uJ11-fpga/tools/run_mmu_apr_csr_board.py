#!/usr/bin/env python3
"""CP39 cold RT-11 FB regression on actual board, FRAM, SD and UART wires."""
import hashlib
import json
import sys
import run_board
from board_common import ROOT
from build_mmu_apr_csr import adapt


def main():
    assert len(sys.argv)==1
    run_board.CORE,run_board.BOARD=adapt(run_board.CORE,run_board.BOARD)
    extras=['tools/run_mmu_apr_csr_board.py','tools/build_mmu_apr_csr.py',
            'build/cp39-csr/inputs.json','build/cp39-csr/lookup.mem']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extras}
    sys.argv=[sys.argv[0],'--tag','cp39']
    run_board.main()
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    record=json.loads((ROOT/'build/cp39-board-inputs.json').read_text())
    record['files'].update(hashes)
    record.update(cpu_apr_csr=True,translation=False,mmr=False,rt11_xm=False)
    (ROOT/'build/cp39-board-verified.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':main()
