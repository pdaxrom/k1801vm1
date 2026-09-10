#!/usr/bin/env python3
"""Cold FB regression for a CP38 bus candidate or its adopted production copy."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import run_board
from board_common import ROOT


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant', default='current', choices=('prefix','split','split-base','priority','current'))
    p.add_argument('--apr', action='store_true')
    p.add_argument('--tag', required=True)
    args = p.parse_args()
    bus = 'boards/hc1200/uj11_board_bus.v' if args.variant=='current' else f'build/cp38-bus/{args.variant}/uj11_board_bus.v'
    run_board.BOARD = [bus if s=='boards/hc1200/uj11_board_bus.v' else s for s in run_board.BOARD]
    extras = ['tools/run_board_decode.py', 'tools/build_board_decode.py', 'build/cp38-bus/inputs.json']
    if args.apr:
        out = ROOT/'build'/(args.tag+'-core'); out.mkdir(parents=True, exist_ok=True)
        board = (ROOT/'boards/hc1200/uj11_board.v').read_text()
        marker = '.clk(clk),.reset(reset),.irq_valid(irq_valid)'
        assert board.count(marker)==1
        copy = out/'uj11_board.v'
        copy.write_text(board.replace(marker, ".mmu_enabled(1'b1),.mmu_hold(1'b0),"+marker))
        run_board.BOARD = [str(copy.relative_to(ROOT)) if s=='boards/hc1200/uj11_board.v' else s for s in run_board.BOARD]
        run_board.CORE = ['build/cp37-lookup/'+Path(s).name if s in (
            'rtl/uj11_core.v','rtl/uj11_engine.v','rtl/uj11_microseq.v',
            'microcode/generated/uj11_decode_table.v') else s for s in run_board.CORE]
        run_board.CORE += ['rtl/experimental/uj11_mmu_entry.v','rtl/uj11_mmu_apr_ram.v']
        extras += ['boards/hc1200/uj11_board.v','build/cp37-lookup/lookup.mem']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extras}
    sys.argv = [sys.argv[0], '--tag', args.tag]
    run_board.main()
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    report = json.loads((ROOT/f'build/{args.tag}-board-inputs.json').read_text())
    report['files'].update(hashes)
    report.update(variant=args.variant,apr_read_only=args.apr,translation=False,rt11_xm=False)
    (ROOT/f'build/{args.tag}-verified.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    main()
