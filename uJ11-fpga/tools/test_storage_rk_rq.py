#!/usr/bin/env python3
"""RK05/MSCP integration with real SERV firmware, SPI pins and 22-bit SRAM DMA."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from serv_test import GUARD,memory_args
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha
from sdcard import Label, add_partition, publish

def fixture(path):
    label=add_partition(Label(1048576,features=1),'rk05',0,boot=True)
    label=add_partition(label,'rk05',7,readonly=True)
    label=add_partition(label,'mscp',0,blocks=311300)
    label=add_partition(label,'mscp',1,blocks=311300,readonly=True)
    with path.open('w+b') as f:
        f.truncate(label.blocks*512);publish(f,label)
        for p,offset in zip(label.partitions,(0,100,200,300)):
            for lba in range(40):
                f.seek((p.start+lba)*512)
                f.write(bytes((((lba+offset)*17)^(j*3)^(j>>8))&255 for j in range(512)))
    return label

def run(out,vendor=None,fast_memory=False):
    if os.environ.get('UJ11_MMU_IOP')!='storage':raise ValueError('Set UJ11_MMU_IOP=storage and UJ11_MMU_FPP=off')
    record=build();out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+BOARD+[GUARD,'tests/mmu/tb_storage_rk_rq.v','tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    record['test_files']={p:sha(ROOT/p) for p in inventory+['tools/test_storage_rk_rq.py','tools/sdcard.py','tools/serv_test.py']}
    if vendor:
        extra=['-DUJ11_VENDOR_ROM','-DUJ11_IOP_VENDOR_RAM']+[str(vendor/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_storage_rk_rq','-o',str(out/'sim')]+extra+inventory
        sim=['vvp',str(out/'sim')]
    else:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD','--top-module','tb_storage_rk_rq','-j','4','--Mdir',str(out/'obj')]+inventory
        sim=[str(out/'obj/Vtb_storage_rk_rq')]
    if fast_memory:
        cmd.append('-Ptb_storage_rk_rq.FAST_MEMORY=1' if vendor else '-GFAST_MEMORY=1')
    record['fast_memory_with_pal']=fast_memory
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    path=out/'controllers.img';fixture(path);before=sha(path)
    with (out/'controllers.log').open('w') as log:
        subprocess.run(sim+memory_args(record)+[f'+SD_IMAGE={path}'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    text=(out/'controllers.log').read_text();print(text[-1500:])
    assert 'PASS MMU' in text and sha(path)==before
    cases=[dict(name='controllers',image_sha256=before)]
    assert all(sha(ROOT/p)==h for p,h in record['test_files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record,cases=cases),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-storage-rk-rq')
    p.add_argument('--vendor-library',type=Path)
    p.add_argument('--fast-memory',action='store_true',help='50 MHz SRAM fast-response, PAL arbiter and concurrent video during RQ DMA')
    a=p.parse_args();run(a.out.resolve(),a.vendor_library,a.fast_memory)
