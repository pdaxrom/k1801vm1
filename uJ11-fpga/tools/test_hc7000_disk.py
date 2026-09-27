#!/usr/bin/env python3
"""Run actual RV32I firmware against the wire-level SD/SRAM models."""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT, sources
from build_iop import build, sha, OUT as FW

def run(out,icarus=False,vendor_library=None,memory=False):
    firmware=build()
    out.mkdir(parents=True,exist_ok=True)
    _,board,_=sources('hc7000-lcd-sram')
    top='tb_hc7000_iop_ram' if memory else 'tb_hc7000_disk'
    inventory=['tests/'+top+'.v','tests/models/spi_sd_model.v','tests/models/async_sram_model.v']
    inventory += [p for p in board if p.startswith('vendor/serv/') or Path(p).stem in
        ('uj11_disk','uj11_rk611','uj11_sector_engine','uj11_sram','uj11_sram_arbiter','spi_byte_service','uj11_iop_ram','uj11_sector_ram')]
    record=dict(firmware=firmware,files={p:sha(ROOT/p) for p in inventory})
    (out/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
    if icarus:
        vendor=[]
        if vendor_library:
            vendor=['-DUJ11_IOP_VENDOR_RAM']+[str(vendor_library/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
            record['vendor_files']={p:sha(Path(p)) for p in vendor[1:]}
            (out/'inputs.json').write_text(json.dumps(record,indent=2)+'\n')
        cmd=['iverilog','-g2012','-s',top,'-o',str(out/'sim')]+inventory+vendor
        sim=['vvp',str(out/'sim')]
    else:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
             '--top-module',top,'-j','4','--Mdir',str(out/'obj')]+inventory
        sim=[str(out/'obj'/('V'+top))]
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:rc=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    print((out/'simulation.log').read_text()[-3000:])
    if rc:raise SystemExit(rc)
    assert ('PASS HC7000 IOP RAM:' if memory else 'PASS HC7000 SERV disk:') in (out/'simulation.log').read_text()
    assert all(sha(ROOT/p)==h for p,h in record['files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,files={p:sha(out/p) for p in
        ('inputs.json','build.log','simulation.log')}),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-hc7000-disk')
    p.add_argument('--icarus',action='store_true')
    p.add_argument('--vendor-library',type=Path,help='Diamond cae_library/simulation/verilog/machxo2 (Icarus)')
    p.add_argument('--memory',action='store_true',help='test every EBR word and byte lane')
    a=p.parse_args();run(a.out.resolve(),a.icarus or bool(a.vendor_library),a.vendor_library,a.memory)
