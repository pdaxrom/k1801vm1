#!/usr/bin/env python3
"""Measure warm CPU/FRAM costs on the unchanged full MMU-less board."""
import hashlib
import json
from pathlib import Path
import subprocess
from board_common import ROOT, CORE, BOARD


def main():
    out=ROOT/'build/cp51-bench';out.mkdir(parents=True,exist_ok=True)
    sources=['tb/tb_board_bench_cp51.v']+CORE+BOARD+[
        'rtl/uj11_rom.v','reference/lsi11/spi_fram_model.v']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+[
        'microcode/generated/m0.mem','microcode/generated/decode.mem',
        'microcode/generated/firmware.mem','tools/run_board_bench_cp51.py','tools/board_common.py']}
    compiled=sources.copy()
    for i,name in enumerate(compiled):
        if name.startswith('reference/'):
            path=out/Path(name).name
            path.write_text('/* verilator lint_off WIDTH */\n'+(ROOT/name).read_text()+
                            '\n/* verilator lint_on WIDTH */\n')
            compiled[i]=str(path)
    top='tb_board_bench_cp51'
    command=['verilator','--binary','--timing','-j','4','--top-module',top,
             '--Mdir',str(out/'obj')]+compiled
    with (out/'build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'run.log').open('w') as log:
        subprocess.run([str(out/'obj'/('V'+top))],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    rows=json.loads((ROOT/'build/cp51-board-bench.json').read_text())
    assert len(rows)==9 and all(r['instructions']==256 for r in rows)
    for row in rows:
        row['cpi']=row['microclocks']/row['instructions']
        row['fram_busy_percent']=100*row['fram_busy_clocks']/row['microclocks']
        row['nominal_instructions_per_second']=29560000/row['cpi']
    report=dict(scope='Warm full native board simulation; forced boot overlay off; guest register setup; no SD or hardware run',
        clock_hz=29560000,spi_hz=14780000,mmu=False,prefetch=False,
        sources_sha256=hashes,workloads=rows)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print((out/'run.log').read_text())


if __name__=='__main__':main()
