#!/usr/bin/env python3
"""Verify the selected CP53 board with unmodified Lattice EBR simulation models."""
import argparse
import hashlib
import json
from board_common import ROOT
from build_cursor_cp53 import adapt, VARIANTS
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('variant',choices=VARIANTS)
    args=p.parse_args()
    core,board=adapt(args.variant)
    out=ROOT/'build/cp53-vendor'/args.variant
    out.mkdir(parents=True,exist_ok=True)
    runner.OUT=out
    bench=(ROOT/'tb/tb_board_bench_cp51.v').read_text()
    bench=replace_once(bench,'sck_edges-start_sck!=12288',
        'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
    bench=replace_once(bench,'module tb_board_bench_cp51;',
        "module tb_board_bench_cp51;\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));")
    bench=bench.replace('build/cp51-board-bench.json',str((out/'bench.json').relative_to(ROOT)))
    bench=bench.replace('PASS CP51','PASS CP53 vendor '+args.variant)
    (out/'tb_bench.v').write_text(bench)
    run=runner.simulate('tb_board_bench_cp51','bench',
        [str((out/'tb_bench.v').relative_to(ROOT))]+core+board+[
            'microcode/generated/uj11_m0_ebr.v','reference/lsi11/spi_fram_model.v']+[
            'build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')],['-DUJ11_VENDOR_ROM'])
    rows=json.loads((out/'bench.json').read_text())
    portable=ROOT/'tb/reports/cp53/cp53-tests/result.json'
    reference=next(v for v in json.loads(portable.read_text())['variants'] if v['variant']==args.variant)
    assert rows==reference['workloads'], 'Lattice/portable benchmark counters differ'
    paths=['tools/run_cursor_vendor_cp53.py','tools/run_fram_cp52.py','tools/build_cursor_cp53.py',
        'tools/build_fram_cp52.py','tools/board_common.py','tb/tb_board_bench_cp51.v',
        str(portable.relative_to(ROOT)),'build/cp53-cursor/inputs.json']
    report=dict(variant=args.variant,run=run,workloads=rows,portable_counters_identical=True,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS selected CP53: all nine vendor workloads exactly match portable counters')


if __name__=='__main__':main()
