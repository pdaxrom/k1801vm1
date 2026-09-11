#!/usr/bin/env python3
"""Protocol, board overlay and CP51 workload comparisons for the CP52 candidate."""
import hashlib
import json
from pathlib import Path
import subprocess
from board_common import ROOT
from build_fram_cp52 import adapt, replace_once

OUT=ROOT/'build/cp52-tests'


def simulate(top, tag, sources, flags=(), verilator=False):
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}
    compiled=sources.copy()
    if verilator:
        for i,name in enumerate(compiled):
            if name.startswith('reference/'):
                copy=OUT/Path(name).name
                copy.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+
                                '\n/* verilator lint_on WIDTH */\n')
                compiled[i]=str(copy)
        command=['verilator','--binary','--timing','-j','4','--top-module',top,
                 '--Mdir',str(OUT/('obj-'+tag))]+list(flags)+compiled
        executable=[str(OUT/('obj-'+tag)/('V'+top))]
    else:
        command=['iverilog','-g2012','-s',top,'-o',str(OUT/tag)]+list(flags)+compiled
        executable=['vvp',str(OUT/tag)]
    with (OUT/(tag+'-build.log')).open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (OUT/(tag+'.log')).open('w') as log:
        subprocess.run(executable,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    passed=[s for s in (OUT/(tag+'.log')).read_text().splitlines() if s.startswith('PASS')]
    assert passed,tag
    print('\n'.join(passed),flush=True)
    return dict(tag=tag,pass_lines=passed,sources_sha256=hashes)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    core,board=adapt()
    fram='build/cp52-fram/uj11_board_fram.v'
    model='reference/lsi11/spi_fram_model.v'
    # Keep the established full-memory random scoreboard, enable retention.
    random=(ROOT/'tb/tb_board_fram.v').read_text()
    random=replace_once(random,'bank,address,data,value','bank,1\'b1,1\'b0,address,data,value')
    (OUT/'tb_board_fram.v').write_text(random)
    runs=[]
    for divisor in (1,3):
        runs.append(simulate('tb_board_fram',f'random-{divisor}',
            ['build/cp52-tests/tb_board_fram.v',fram,model],
            [f'-Ptb_board_fram.CLK_DIV={divisor}']))
        runs.append(simulate('tb_fram_cp52',f'protocol-{divisor}',
            ['tb/tb_fram_cp52.v',fram,model],[f'-Ptb_fram_cp52.CLK_DIV={divisor}']))
    runs.append(simulate('tb_board_bus','bus',['tb/tb_board_bus.v']+board+[model]))
    overlay=(ROOT/'tb/tb_board_bus.v').read_text().replace('tb_board_bus','tb_bus_fram_cp52')
    overlay=replace_once(overlay,'        $display("PASS board bus:',
        (ROOT/'tb/bus_fram_cp52_checks.vh').read_text()+'\n        $display("PASS board bus:')
    (OUT/'tb_bus_fram_cp52.v').write_text(overlay)
    runs.append(simulate('tb_bus_fram_cp52','overlays',['build/cp52-tests/tb_bus_fram_cp52.v']+board+[model]))
    # Identical guest streams, warmup and measurement interval to the frozen
    # CP51 fixture; only output naming and exact expected wire counts change.
    bench=(ROOT/'tb/tb_board_bench_cp51.v').read_text().replace('cp51','cp52').replace('CP51','CP52')
    bench=replace_once(bench,'sck_edges-start_sck!=12288',
        'sck_edges-start_sck!=(workload==7 ? 12288 : 4224)')
    (OUT/'tb_board_bench_cp52.v').write_text(bench)
    runs.append(simulate('tb_board_bench_cp52','bench',
        ['build/cp52-tests/tb_board_bench_cp52.v']+core+board+['rtl/uj11_rom.v',model],verilator=True))
    vendor_bench=bench.replace('module tb_board_bench_cp52;',
        "module tb_board_bench_cp52;\n    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));")
    vendor_bench=vendor_bench.replace('build/cp52-board-bench.json','build/cp52-vendor-bench.json')
    (OUT/'tb_board_bench_vendor.v').write_text(vendor_bench)
    runs.append(simulate('tb_board_bench_cp52','bench-vendor',
        ['build/cp52-tests/tb_board_bench_vendor.v']+core+board+[
            'microcode/generated/uj11_m0_ebr.v',model]+[
            'build/vendor/'+n+'.v' for n in ('DP8KC','GSR','PUR')],['-DUJ11_VENDOR_ROM']))
    rows=json.loads((ROOT/'build/cp52-board-bench.json').read_text())
    assert rows==json.loads((ROOT/'build/cp52-vendor-bench.json').read_text()), 'portable/vendor benchmark mismatch'
    baseline=json.loads((ROOT/'tb/reports/cp51/result.json').read_text())['workloads']
    assert len(rows)==len(baseline)==9
    for row,ref in zip(rows,baseline):
        for key in ('workload','instructions','memory_beats','opcode_fetches','writes'):
            assert row[key]==ref[key],(row['workload'],key)
        assert row['microclocks']<=ref['microclocks'],row['workload']
        row.update(cpi=row['microclocks']/row['instructions'],
            speedup=ref['microclocks']/row['microclocks'],
            nominal_instructions_per_second=29560000*row['instructions']/row['microclocks'])
    inputs=['tools/run_fram_cp52.py','tools/build_fram_cp52.py','tools/board_common.py',
            'tb/tb_board_fram.v','tb/tb_board_bench_cp51.v','tb/bus_fram_cp52_checks.vh','tb/reports/cp51/result.json',
            'microcode/generated/m0.mem','microcode/generated/decode.mem','microcode/generated/firmware.mem',
            'build/cp52-fram/inputs.json']
    report=dict(mmu=False,prefetch=False,production_changed=False,runs=runs,workloads=rows,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        logs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.glob('*.log'))})
    (OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
