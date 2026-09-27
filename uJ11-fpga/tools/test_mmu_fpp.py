#!/usr/bin/env python3
"""Execute the shared DEC FP11 differential fixtures on the MMU CPU microcode."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu import build,CORE
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
from fpp_reference import prepare

def run(out,families=None,smoke=False):
    out.mkdir(parents=True,exist_ok=True);hw=build()
    reference=out/'reference';adaptation=prepare(reference)
    oracle=out/'oracle';all_vectors=out/'all-vectors.txt'
    sources=['tests/reference_fpp.c','tests/reference_fpp_add.h','tests/reference_fpp_muldiv.h','tools/fpp_reference.py']
    oracle_inputs={p:sha(ROOT/p) for p in sources}|adaptation['original']
    stamp=out/'oracle-inputs.json'
    if not all_vectors.exists() or not stamp.exists() or json.loads(stamp.read_text())!=oracle_inputs:
        subprocess.run(['cc','-O2','-Wall','-DENABLE_MMU=0','-I..','tests/reference_fpp.c',str(reference/'core.c'),str(reference/'hardware.c'),'-lm','-o',str(oracle)],cwd=ROOT,capture_output=True,check=True)
        with all_vectors.open('w') as f,(out/'oracle.log').open('w') as err:
            subprocess.run([str(oracle)],cwd=ROOT,stdout=f,stderr=err,check=True)
        stamp.write_text(json.dumps(oracle_inputs,indent=2)+'\n')
    count=0
    with all_vectors.open() as rows,(out/'vectors.txt').open('w') as selected:
        for row in rows:
            v=row.split();op=int(v[0],16);seed=int(v[78],16);fault=int(v[13],16)
            if families and (op&0o177400) not in families:continue
            if smoke and not (seed in (0,1,48,49,50,51) or seed>=512 and seed&511 in (0,1,8,9,57,255)):continue
            selected.write(row);count+=1
    inventory=CORE+['build/hc7000-mmu-hardware/uj11_mmu_rom.v','tests/mmu/tb_fpp.v']
    hashes={p:sha(ROOT/p) for p in inventory}
    cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD','--top-module','tb_fpp','-j','4','--Mdir',str(out/'obj')]+inventory
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:rc=subprocess.run([str(out/'obj/Vtb_fpp'),f'+VECTORS={out}/vectors.txt'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    print((out/'simulation.log').read_text()[-4000:],flush=True)
    if rc:raise SystemExit(rc)
    assert f'PASS MMU FPP: {count} cases' in (out/'simulation.log').read_text()
    assert all(sha(ROOT/p)==h for p,h in hashes.items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,cases=count,hardware=hw,files=hashes,oracle=adaptation,vectors_sha256=sha(out/'vectors.txt')),indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'build/test-mmu-fpp');p.add_argument('--families',nargs='+',type=lambda s:int(s,8));p.add_argument('--smoke',action='store_true');a=p.parse_args();run(a.out.resolve(),a.families,a.smoke)
