#!/usr/bin/env python3
"""Compare CPU cycle cost at 24/50 MHz with fixed six-cycle external memory.

This is an RTL workload measurement, not physical timing qualification or a
whole-board benchmark. Run MAP/PAR/TRACE and OS boot tests separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from board_common import ROOT
from build_mmu import CORE, MICROCODE, rom, assemble


def run(out, mapped=False):
    out.mkdir(parents=True,exist_ok=False)
    bench='tests/mmu/tb_cpu_perf.v'
    sources=CORE+MICROCODE[:2]+[bench,'tools/test_cpu_perf_mmu.py','tools/build_mmu.py','microasm/uj11mmuasm.py']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes={p:sha(ROOT/p) for p in sources}
    image,_,_,stats=assemble('\n'.join((ROOT/p).read_text() for p in MICROCODE[:2]))
    mem=out/'microcode.mem';mem.write_text(''.join(f'{w:014x}\n' for w in image))
    rows={};hardware={}
    for mhz in (24,50):
        hardware[mhz]=dict(clock_mhz=mhz,pipeline=mhz==50,fpp='off',microcode_words=stats['used_words'])
        source=out/f'rom-{mhz}.v';source.write_text(rom(image,'off',mhz==50));exe=out/f'perf-{mhz}'
        subprocess.run(['iverilog','-g2012','-s','tb_cpu_perf',f'-Ptb_cpu_perf.CLOCK_HZ={mhz*1000000}',
            f'-Ptb_cpu_perf.MMU_ENABLE={int(mapped)}',
            f'-Ptb_cpu_perf.IMAGE="{mem}"','-o',str(exe),bench]+CORE+[str(source)],cwd=ROOT,check=True)
        result=subprocess.run(['vvp',str(exe)],cwd=ROOT,capture_output=True,text=True,check=True).stdout
        (out/f'{mhz}.log').write_text(result)
        rows[mhz]=[dict(workload=int(m[1]),cycles=int(m[2]),state=m[3])
            for m in re.finditer(r'PERF (\d) cycles=(\d+) (.+)',result)]
        assert len(rows[mhz])==4
        profiles={int(m[1]):{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',m[2])}
            for m in re.finditer(r'PROFILE (\d) (.+)',result)}
        assert len(profiles)==4
        for row in rows[mhz]:row['profile']=profiles[row['workload']]
    comparisons=[]
    for a,b in zip(rows[24],rows[50]):
        assert a['state']==b['state'],(a,b)
        comparisons.append(dict(workload=a['workload'],cycles_24=a['cycles'],cycles_50=b['cycles'],
            speedup=(a['cycles']/24)/(b['cycles']/50)))
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    record=dict(passed=True,scope=__doc__,mapped=mapped,loop_only=mapped,
        files=hashes,hardware=hardware,results=rows,comparisons=comparisons)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(comparisons,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mapped',action='store_true',help='enable MMU and profile just the guest loops')
    a=p.parse_args();run(a.out.resolve(),a.mapped)
