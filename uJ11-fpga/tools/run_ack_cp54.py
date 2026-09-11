#!/usr/bin/env python3
"""Check CP54 bus side effects and exact full-board workload counters."""
import argparse
import hashlib
import json
from board_common import ROOT
from build_ack_cp54 import adapt, VARIANTS
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vendor',choices=VARIANTS,help='Only selected variant, unmodified Lattice EBR models')
    args=p.parse_args()
    summaries=[]
    for variant in ((args.vendor,) if args.vendor else VARIANTS):
        core,board=adapt(variant)
        out=ROOT/('build/cp54-vendor' if args.vendor else 'build/cp54-tests')/variant
        out.mkdir(parents=True,exist_ok=True);runner.OUT=out
        runs=[]
        if not args.vendor:
            bus=replace_once((ROOT/'tb/tb_board_bus.v').read_text(),'        $display("PASS board bus:',
                (ROOT/'tb/bus_fram_cp52_checks.vh').read_text()+'\n        $display("PASS board bus:')
            (out/'tb_board_bus.v').write_text(bus)
            runs.append(runner.simulate('tb_board_bus','bus',
                [str((out/'tb_board_bus.v').relative_to(ROOT))]+board+['reference/lsi11/spi_fram_model.v']))
        bench=replace_once((ROOT/'tb/tb_board_bench_cp51.v').read_text(),'sck_edges-start_sck!=12288',
            'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
        bench=bench.replace('build/cp51-board-bench.json',str((out/'bench.json').relative_to(ROOT)))
        bench=bench.replace('PASS CP51','PASS CP54 '+variant)
        sources=core+board+['reference/lsi11/spi_fram_model.v']
        if args.vendor:
            bench=replace_once(bench,'module tb_board_bench_cp51;',
                "module tb_board_bench_cp51;\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));")
            sources+=['microcode/generated/uj11_m0_ebr.v']+['build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')]
        else:sources+=['rtl/uj11_rom.v']
        (out/'tb_bench.v').write_text(bench)
        runs.append(runner.simulate('tb_board_bench_cp51','bench',
            [str((out/'tb_bench.v').relative_to(ROOT))]+sources,
            ['-DUJ11_VENDOR_ROM'] if args.vendor else [],verilator=not args.vendor))
        rows=json.loads((out/'bench.json').read_text())
        reference=json.loads((ROOT/'docs/synthesis-cp53.json').read_text())['vendor_workloads']
        assert rows==reference, (variant,'CP53 benchmark counters changed')
        summaries.append(dict(variant=variant,vendor=bool(args.vendor),runs=runs,workloads=rows,cp53_counters_identical=True))
    inputs=['tools/run_ack_cp54.py','tools/run_fram_cp52.py','tools/build_ack_cp54.py','tools/build_fram_cp52.py',
        'tools/board_common.py','tb/tb_board_bus.v','tb/bus_fram_cp52_checks.vh','tb/tb_board_bench_cp51.v',
        'microcode/generated/m0.mem','microcode/generated/decode.mem','microcode/generated/firmware.mem',
        'build/cp54-ack/inputs.json','docs/synthesis-cp53.json']
    folder=out.parent
    report=dict(variants=summaries,inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*.log')})
    (folder/('result-'+args.vendor+'.json' if args.vendor else 'result.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP54 board tests: all CP53 workload counters preserved')


if __name__=='__main__':main()
