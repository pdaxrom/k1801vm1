#!/usr/bin/env python3
"""Cold RT-11FB board regression for the CP37d/e read-only APR lookup."""
import hashlib
import json
from pathlib import Path
import sys
import run_board
from board_common import ROOT


def main():
    # Keep the established board scoreboard, UART wire check and SD overlay.
    # This is the fixed enabled/hold configuration measured by CP37d/e.
    out = ROOT/'build/cp37-board'
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT/'boards/hc1200/uj11_board.v').read_text()
    marker = '.clk(clk),.reset(reset),.irq_valid(irq_valid)'
    assert source.count(marker) == 1
    copy = out/'uj11_board.v'
    copy.write_text(source.replace(marker, ".mmu_enabled(1'b1),.mmu_hold(1'b0),"+marker))
    run_board.CORE = ['build/cp37-lookup/'+Path(s).name if s in (
        'rtl/uj11_core.v', 'rtl/uj11_engine.v', 'rtl/uj11_microseq.v',
        'microcode/generated/uj11_decode_table.v') else s for s in run_board.CORE]
    run_board.CORE += ['rtl/experimental/uj11_mmu_entry.v', 'rtl/uj11_mmu_apr_ram.v']
    run_board.BOARD = [str(copy.relative_to(ROOT)) if s == 'boards/hc1200/uj11_board.v'
                       else s for s in run_board.BOARD]
    extra = ['tools/run_mmu_apr_lookup_board.py', 'tools/build_mmu_entry.py','tools/build_mmu_apr_lookup.py','microasm/uj11aprasm.py','microcode/mmu_apr_lookup.uasm',
             'microasm/uj11entryasm.py', 'microcode/mmu_entry.uasm',
             'build/cp37-lookup/lookup.mem', 'boards/hc1200/uj11_board.v']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extra}
    assert len(sys.argv) == 1, 'fixed CP37d/e portable FB regression; no extra arguments'
    sys.argv += ['--tag', 'cp37-entry']
    run_board.main()
    for p, expected in hashes.items():
        assert hashlib.sha256((ROOT/p).read_bytes()).hexdigest() == expected, p
    report = json.loads((ROOT/'build/cp37-entry-board-inputs.json').read_text())
    report['files'].update(hashes)
    report.update(context_enabled=True, hold=False, apr_lookup_read_only=True, translation=False, rt11_xm=False)
    (ROOT/'build/cp37-entry-board.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
