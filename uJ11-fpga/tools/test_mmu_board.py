#!/usr/bin/env python3
"""Physical board/18-bit SERV DMA tests against actual SD and SRAM pins."""
import argparse
import json
import subprocess
from pathlib import Path
from board_common import ROOT
from build_mmu_board import build,CORE,BOARD,sha

def run(out,name,vendor=None,image=None,monitor='fb'):
    record=build();out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+BOARD+['tests/mmu/'+name+'.v',
        'tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    record['test_files']={p:sha(ROOT/p) for p in inventory}
    extra=[]
    if vendor:
        extra=['-DUJ11_VENDOR_ROM','-DUJ11_IOP_VENDOR_RAM']+[str(vendor/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
        record['vendor_files']={p:sha(Path(p)) for p in extra[2:]}
        cmd=['iverilog','-g2012','-s',name,'-o',str(out/'sim')]+extra+inventory
        sim=['vvp',str(out/'sim')]
    else:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
             '--top-module',name,'-j','4','--Mdir',str(out/'obj')]+inventory
        sim=[str(out/'obj'/('V'+name))]
    if name=='tb_mmu_boot':
        image=image or ROOT/'../lsi11-fpga/images/rt11v503.dsk'
        image_hash=sha(image)
        sim += [f'+SD_IMAGE={image.resolve()}',f'+UART_LOG={out}/uart.txt',f'+MONITOR={monitor}']
        record['sd_image_sha256']=image_hash
        record['sd_image']=str(image.resolve())
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:rc=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    print((out/'simulation.log').read_text()[-3000:])
    if rc:raise SystemExit(rc)
    if name=='tb_mmu_boot':assert sha(image)==image_hash
    assert 'PASS MMU' in (out/'simulation.log').read_text()
    assert all(sha(ROOT/p)==h for p,h in record['test_files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-mmu-board')
    p.add_argument('--name',choices=('tb_mmu_disk','tb_mmu_bus','tb_mmu_boot'),default='tb_mmu_disk')
    p.add_argument('--vendor-library',type=Path)
    p.add_argument('--image',type=Path)
    p.add_argument('--monitor',choices=('fb','xm'),default='fb')
    a=p.parse_args();run(a.out.resolve(),a.name,a.vendor_library,a.image,a.monitor)
