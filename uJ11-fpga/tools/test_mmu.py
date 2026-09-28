#!/usr/bin/env python3
"""Isolated MMU/CPU/console regression; does not claim board qualification."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu import build,CORE,OUT as HARDWARE

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def run(out,vendor=None):
    hardware=build();out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+['build/hc7000-mmu-hardware/uj11_mmu_rom.v','boards/hc7000/mmu/uj11_mmu_sram_arbiter.v']
    tests=['tb_decode','tb_rom','tb_translate','tb_mmu','tb_mmu_cache','tb_cpu','tb_csm','tb_locked','tb_cpu_events']
    if hardware['fpp']=='microcode':tests.append('tb_fp_datapath')
    hashes={p:sha(ROOT/p) for p in inventory+['tests/mmu/'+n+'.v' for n in tests]}
    extra=[]
    if vendor:
        extra=['-DUJ11_VENDOR_ROM']+[str(vendor/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
    results={}
    for name in tests:
        cmd=['iverilog','-g2012','-s',name,'-o',str(out/name)]+extra+['tests/mmu/'+name+'.v']+inventory
        with (out/(name+'-build.log')).open('w') as log:
            subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(['vvp',str(out/name)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        text=(out/(name+'.log')).read_text();assert 'PASS MMU' in text,text
        print(text[-1000:]);results[name]=dict(passed=True,log_sha256=sha(out/(name+'.log')))
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    record=dict(hardware=hardware,files=hashes,tests=results,passed=True)
    if vendor:record['vendor_files']={p:sha(Path(p)) for p in extra[1:]}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-mmu')
    p.add_argument('--vendor-library',type=Path)
    a=p.parse_args();run(a.out.resolve(),a.vendor_library)
