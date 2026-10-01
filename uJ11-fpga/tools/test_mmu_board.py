#!/usr/bin/env python3
"""Physical board/18-bit SERV DMA tests against actual SD and SRAM pins."""
import argparse
import json
import subprocess
from pathlib import Path
from serv_test import GUARD,memory_args
from board_common import ROOT
from build_mmu_board import build,CORE,BOARD,sha

def run(out,name,vendor=None,image=None,monitor='fb',boot_menu=False,profile=False):
    record=build();out.mkdir(parents=True,exist_ok=True)
    inventory=CORE+BOARD+[GUARD,'tests/mmu/'+name+'.v',
        'tests/models/async_sram_model.v','tests/models/spi_sd_model.v','tests/models/terminal_monitor.v']
    record['test_files']={p:sha(ROOT/p) for p in inventory+['tools/test_mmu_board.py','tools/serv_test.py']}
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
    if name in ('tb_mmu_boot','tb_mmu_bsd','tb_storage_bus'):
        image=image or ROOT/'../lsi11-fpga/images/rt11v503.dsk'
        image_hash=sha(image)
        sim += [f'+SD_IMAGE={image.resolve()}',f'+UART_LOG={out}/uart.txt',f'+MONITOR={monitor}']
        record['sd_image_sha256']=image_hash
        record['sd_image']=str(image.resolve())
        record['boot_menu']=boot_menu
        if boot_menu:sim.append('+BOOT_MENU')
    if name=='tb_mmu_bsd':
        hz=record['clock_mhz']*1000000
        cmd.append(f'-P{name}.CLOCK_HZ={hz}' if vendor else f'-GCLOCK_HZ={hz}')
        for param in ('VIDEO_ENABLE','TERMINAL_ENABLE'):
            value=int(record['video' if param=='VIDEO_ENABLE' else 'terminal'])
            cmd.append(f'-P{name}.{param}={value}' if vendor else f'-G{param}={value}')
    if profile:
        assert name=='tb_mmu_bsd', 'profiling requires the BSD board test'
        sim.append('+PROFILE_BSD')
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:rc=subprocess.run(sim+memory_args(record),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
    print((out/'simulation.log').read_text()[-3000:])
    if rc:raise SystemExit(rc)
    if name in ('tb_mmu_boot','tb_mmu_bsd','tb_storage_bus'):assert sha(image)==image_hash
    assert ('PASS PAL SERV bus' if name=='tb_pal_bus' else 'PASS MMU') in (out/'simulation.log').read_text()
    if profile:
        lines=(out/'simulation.log').read_text().splitlines()
        commands=[json.loads(s.removeprefix('BSD_PROFILE ')) for s in lines if s.startswith('BSD_PROFILE ')]
        upc={s.split()[1]:int(s.split()[2]) for s in lines if s.startswith('BSD_UPC ')}
        assert len(commands)==8 and upc
        (out/'profile.json').write_text(json.dumps(dict(clock_hz=hz,commands=commands,microsteps=upc),indent=2)+'\n')
    assert all(sha(ROOT/p)==h for p,h in record['test_files'].items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,inputs=record),indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/test-mmu-board')
    p.add_argument('--name',choices=('tb_mmu_disk','tb_mmu_bus','tb_mmu_boot','tb_mmu_bsd','tb_storage_bus','tb_pal_bus'),default='tb_mmu_disk')
    p.add_argument('--vendor-library',type=Path)
    p.add_argument('--image',type=Path)
    p.add_argument('--monitor',choices=('fb','xm'),default='fb')
    p.add_argument('--boot-menu',action='store_true',help='allow the five-second SD menu autoboot')
    p.add_argument('--profile',action='store_true',help='profile BSD commands with nl0 cr0, CPU and disk counters')
    a=p.parse_args();run(a.out.resolve(),a.name,a.vendor_library,a.image,a.monitor,a.boot_menu,a.profile)
