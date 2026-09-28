#!/usr/bin/env python3
"""SERV storage integration: real RV32I firmware, SPI pins and 18-bit SRAM DMA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from serv_test import GUARD,memory_args
from board_common import ROOT
from build_mmu_board import build, CORE, BOARD, sha
from sdcard import Label, add_partition, publish

def fixture(path, damaged=0, boot_kind=None):
    label=add_partition(Label(262144 if boot_kind else 131072),'rk07',0)
    label=add_partition(label,'rk07',7,boot=boot_kind is None,readonly=True)
    if boot_kind:label=add_partition(label,boot_kind,1,boot=True)
    with path.open('w+b') as f:
        f.truncate(label.blocks*512);publish(f,label)
        for p,offset in zip(label.partitions,(0,100)):
            for lba in range(12):
                f.seek((p.start+lba)*512)
                f.write(bytes((((lba+offset)*17)^(j*3)^(j>>8))&255 for j in range(512)))
        for copy in range(damaged):
            f.seek(copy*512+100);f.write(b'\xff')
    return label

def run(out,vendor=None):
    if os.environ.get('UJ11_MMU_IOP')!='storage':raise ValueError('Set UJ11_MMU_IOP=storage and UJ11_MMU_FPP=off')
    record=build();out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+BOARD+[GUARD,'tests/mmu/tb_storage_disk.v','tests/models/async_sram_model.v','tests/models/spi_sd_model.v']
    record['test_files']={p:sha(ROOT/p) for p in inventory+['tools/test_storage.py','tools/sdcard.py','tools/serv_test.py']}
    if vendor:
        extra=['-DUJ11_VENDOR_ROM','-DUJ11_IOP_VENDOR_RAM']+[str(vendor/(n+'.v')) for n in ('DP8KC','PDPW8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_storage_disk','-o',str(out/'sim')]+extra+inventory
        sim=['vvp',str(out/'sim')]
    else:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD','--top-module','tb_storage_disk','-j','4','--Mdir',str(out/'obj')]+inventory
        sim=[str(out/'obj/Vtb_storage_disk')]
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    cases=[]
    for name,damage,boot_kind,error in [('primary',0,None,0),('backup',1,None,0),('invalid',2,None,6),('unsupported',0,'rk06',7)]:
        path=out/(name+'.img');fixture(path,damage,boot_kind);before=sha(path)
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(sim+memory_args(record)+[f'+SD_IMAGE={path}']+([f'+FAIL_INIT={error}'] if error else []),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        text=(out/(name+'.log')).read_text();print(text[-1500:])
        assert 'PASS MMU' in text and sha(path)==before
        cases.append(dict(name=name,image_sha256=before))
    assert all(sha(ROOT/p)==h for p,h in record['test_files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record,cases=cases),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-storage')
    p.add_argument('--vendor-library',type=Path)
    a=p.parse_args();run(a.out.resolve(),a.vendor_library)
