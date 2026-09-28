#!/usr/bin/env python3
"""Differential integer/MMU programs against the repository DCJ11 core."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from board_common import ROOT
from build_mmu import build, CORE

def run(out, vendor=None):
    out.mkdir(parents=True,exist_ok=True)
    hardware=build()
    sources=['tests/reference_integer.c','../core/core.c','../core/core.h','../core/hardware.c',
             '../core/hardware.h','../core/pdp11_fp.c','tests/mmu/tb_integer.v',
             'microcode/uj11-mmu.uasm','tools/test_integer_mmu.py']+CORE+['build/hc7000-mmu-hardware/uj11_mmu_rom.v']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes={p:sha(ROOT/p) for p in sources}
    subprocess.run(['cc','-O2','-DENABLE_MMU=1','-I..','tests/reference_integer.c',
                    '../core/core.c','../core/hardware.c','-o',str(out/'reference')],cwd=ROOT,check=True)
    with (out/'fixtures.txt').open('w') as f:
        subprocess.run([str(out/'reference')],stdout=f,check=True)
    extra=[]
    if vendor:extra=['-DUJ11_VENDOR_ROM']+[str(vendor/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
    with (out/'build.log').open('w') as f:
        subprocess.run(['iverilog','-g2012','-s','tb_integer','-o',str(out/'test')]+extra+
            ['tests/mmu/tb_integer.v']+CORE+['build/hc7000-mmu-hardware/uj11_mmu_rom.v'],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
    with (out/'rtl.log').open('w') as f:
        rc=subprocess.run(['vvp',str(out/'test'),'+fixtures='+str(out/'fixtures.txt')],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT).returncode
    print((out/'rtl.log').read_text()[-5000:])
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    (out/'result.json').write_text(json.dumps(dict(passed=rc==0,hardware=hardware,files=hashes,
        fixtures_sha256=sha(out/'fixtures.txt'),log_sha256=sha(out/'rtl.log')),indent=2)+'\n')
    if rc:raise SystemExit(rc)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--vendor-library',type=Path);a=p.parse_args();run(a.out.resolve(),a.vendor_library)
