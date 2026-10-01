#!/usr/bin/env python3
"""Measure MMU CPU throughput with original/PAL arbiters, video off/4/8 bpp.

Actual CPU, SRAM controller, video DMA, PAL timing and async SRAM model.
No disk/OS/SERV workload: this measures CPU/SRAM contention, not BSD speed.
Each sample covers 160 ms (four full PAL frames) after warm-up.
"""
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build, CORE, VIDEO, sha


def run():
    hw=build()
    assert hw['clock_mhz']==50 and hw['video'] and hw['fpp']=='off'
    out=ROOT/'build/test-pal-cpu-perf';out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+VIDEO+['boards/hc7000/uj11_sram.v','boards/hc7000/mmu/uj11_mmu_sram_arbiter.v',
        'build/hc7000-mmu-hardware/uj11_mmu_rom.v','tests/models/async_sram_model.v',
        'tests/mmu/tb_pal_cpu_perf.v']
    hashes={p:sha(ROOT/p) for p in inventory+['tools/test_pal_cpu_perf.py']}
    def sample(mode):
        obj=out/f'obj-{mode}'
        with (out/f'build-{mode}.log').open('w') as log:
            subprocess.run(['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
                '--top-module','tb_pal_cpu_perf',f'-GMODE={mode}','-j','2','--Mdir',str(obj)]+inventory,
                cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/f'mode-{mode}.log').open('w') as log:
            r=subprocess.run([str(obj/'Vtb_pal_cpu_perf')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        log=(out/f'mode-{mode}.log').read_text();r.check_returncode()
        assert 'PASS PAL CPU performance' in log
        rows=[json.loads(line.removeprefix('PAL_CPU_PERF ')) for line in log.splitlines() if line.startswith('PAL_CPU_PERF ')]
        assert len(rows)==4
        return rows
    with ThreadPoolExecutor(max_workers=2) as pool:
        rows=list(pool.map(sample,range(4)))
    baseline=json.loads((ROOT/'releases/hc7000-pal-preview/validation/test-pal-cpu-perf/result.json').read_text())
    names=['register arithmetic','memory read/modify/write','EIS MUL/DIV/ASH','memory copy']
    comparisons=[]
    for n,name in enumerate(names):
        assert rows[0][n]['instructions']==baseline['results'][0][n]['instructions'],'Original baseline changed'
        assert rows[2][n]['instructions']==rows[3][n]['instructions'],'4/8 bpp traffic differs'
        base=rows[1][n]['instructions'];video=rows[2][n]['instructions']
        comparisons.append(dict(workload=name,instructions_off=base,instructions_video=video,
            throughput_loss_percent=100*(1-video/base),elapsed_increase_percent=100*(base/video-1),
            speedup_vs_fa92_video=video/baseline['results'][2][n]['instructions'],
            speedup_vs_original_no_video=video/rows[0][n]['instructions'],
            speedup_off_vs_original=base/rows[0][n]['instructions']))
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    record=dict(passed=True,scope=__doc__,hardware=hw,files=hashes,results=rows,comparisons=comparisons)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(comparisons,indent=2))


if __name__=='__main__':run()
