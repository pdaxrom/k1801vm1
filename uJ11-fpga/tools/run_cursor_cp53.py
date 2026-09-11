#!/usr/bin/env python3
"""Reuse established FRAM/board fixtures on each CP53 equivalent candidate."""
import hashlib
import json
from board_common import ROOT
from build_cursor_cp53 import adapt, VARIANTS
from build_fram_cp52 import replace_once
import run_fram_cp52 as runner


def main():
    summaries=[]
    ref=json.loads((ROOT/'tb/reports/cp52/tests.json').read_text())['workloads']
    for variant in VARIANTS:
        core,board=adapt(variant)
        out=ROOT/'build/cp53-tests'/variant;out.mkdir(parents=True,exist_ok=True)
        runner.OUT=out
        fram=f'build/cp53-cursor/{variant}/uj11_board_fram.v'
        model='reference/lsi11/spi_fram_model.v'
        random=(ROOT/'tb/tb_board_fram.v').read_text()
        random=replace_once(random,'bank,address,data,value',"bank,1'b1,1'b0,address,data,value")
        (out/'tb_board_fram.v').write_text(random)
        runs=[]
        for divisor in (1,3):
            runs.append(runner.simulate('tb_board_fram',f'random-{divisor}',
                [str((out/'tb_board_fram.v').relative_to(ROOT)),fram,model],[f'-Ptb_board_fram.CLK_DIV={divisor}']))
            runs.append(runner.simulate('tb_fram_cp52',f'protocol-{divisor}',
                ['tb/tb_fram_cp52.v',fram,model],[f'-Ptb_fram_cp52.CLK_DIV={divisor}']))
        overlay=(ROOT/'tb/tb_board_bus.v').read_text()
        overlay=replace_once(overlay,'        $display("PASS board bus:',
            (ROOT/'tb/bus_fram_cp52_checks.vh').read_text()+'\n        $display("PASS board bus:')
        (out/'tb_board_bus.v').write_text(overlay)
        runs.append(runner.simulate('tb_board_bus','bus',[str((out/'tb_board_bus.v').relative_to(ROOT))]+board+[model]))
        bench=(ROOT/'tb/tb_board_bench_cp51.v').read_text()
        bench=replace_once(bench,'sck_edges-start_sck!=12288','sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
        bench=bench.replace('build/cp51-board-bench.json',str((out/'bench.json').relative_to(ROOT)))
        bench=bench.replace('PASS CP51','PASS CP53 '+variant)
        (out/'tb_bench.v').write_text(bench)
        runs.append(runner.simulate('tb_board_bench_cp51','bench',[str((out/'tb_bench.v').relative_to(ROOT))]+core+board+['rtl/uj11_rom.v',model],verilator=True))
        rows=json.loads((out/'bench.json').read_text());assert len(rows)==9
        for row,baseline in zip(rows,ref):
            for k,v in row.items():assert v==baseline[k],(variant,row['workload'],k)
        summaries.append(dict(variant=variant,runs=runs,workloads=rows,all_counters_identical_to_cp52=True))
    inputs=['tools/run_cursor_cp53.py','tools/run_fram_cp52.py','tools/build_cursor_cp53.py','tools/build_fram_cp52.py',
            'tools/board_common.py','tb/tb_board_fram.v','tb/tb_board_bus.v','tb/tb_board_bench_cp51.v',
            'tb/bus_fram_cp52_checks.vh','tb/tb_fram_cp52.v','tb/reports/cp52/tests.json',
            'microcode/generated/m0.mem','microcode/generated/decode.mem','microcode/generated/firmware.mem',
            'build/cp53-cursor/inputs.json']
    report=dict(variants=summaries,inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'build/cp53-tests').rglob('*.log')})
    (ROOT/'build/cp53-tests/result.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP53 all three full-board candidates: all CP52 benchmark counters preserved')


if __name__=='__main__':main()
