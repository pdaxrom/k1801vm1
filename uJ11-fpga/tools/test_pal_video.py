#!/usr/bin/env python3
"""PAL timing, SRAM protocol, CDC and pixels under CPU/disk contention."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from board_common import ROOT
SOURCES=['boards/hc7000/video/'+n+'.v' for n in
         ('uj11_pal_video','uj11_pal_timing','uj11_pal_encoder','uj11_pal_ram','uj11_video_arbiter')]
SOURCES+=['boards/hc7000/uj11_sram.v','tests/models/async_sram_model.v','tests/tb_pal_video.v']
def run(vendor=None):
    out=ROOT/'build'/('test-pal-vendor' if vendor else 'test-pal')
    out.mkdir(parents=True,exist_ok=True)
    if vendor:
        extra=['-DUJ11_VIDEO_VENDOR_RAM']+[str(vendor/(n+'.v')) for n in ('DP8KC','GSR','PUR')]
        cmd=['iverilog','-g2012','-s','tb_pal_video','-o',str(out/'sim')]+extra+SOURCES
        sim=['vvp',str(out/'sim')]
    else:
        cmd=['verilator','--binary','--timing','-Wno-WIDTH','-Wno-TIMESCALEMOD',
             '--top-module','tb_pal_video','-j','4','--Mdir',str(out/'obj')]+SOURCES
        sim=[str(out/'obj/Vtb_pal_video')]
    unit_logs={}
    for name,rtl in [('tb_pal_ram','uj11_pal_ram'),('tb_pal_encoder','uj11_pal_encoder')]:
        unit_extra=(['-DUJ11_VIDEO_VENDOR_RAM']+[str(vendor/(n+'.v')) for n in ('DP8KC','GSR','PUR')]) if vendor and name=='tb_pal_ram' else []
        subprocess.run(['iverilog','-g2012','-s',name,'-o',str(out/name)]+unit_extra+
                       ['tests/'+name+'.v','boards/hc7000/video/'+rtl+'.v'],cwd=ROOT,check=True)
        unit_logs[name]=subprocess.check_output(['vvp',str(out/name)],cwd=ROOT,text=True)
        assert 'PASS PAL' in unit_logs[name]
        (out/(name+'.log')).write_text(unit_logs[name]);print(unit_logs[name])
    record={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in SOURCES+['tools/test_pal_video.py','tests/tb_pal_ram.v','tests/tb_pal_encoder.v']}
    with (out/'build.log').open('w') as log:subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (out/'simulation.log').open('w') as log:r=subprocess.run(sim,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    print((out/'simulation.log').read_text()[-3000:]);r.check_returncode()
    assert 'PASS PAL framebuffer' in (out/'simulation.log').read_text()
    (out/'result.json').write_text(json.dumps(dict(passed=True,files=record),indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vendor-library',type=Path)
    a=p.parse_args();run(a.vendor_library)
