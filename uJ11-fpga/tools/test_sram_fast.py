#!/usr/bin/env python3
"""Four-state SRAM/arbiter validation at 50 MHz with board timing budgets."""
import hashlib
import json
import subprocess
from board_common import ROOT


def run():
    out=ROOT/'build/test-sram-fast';out.mkdir(parents=True,exist_ok=True)
    sources=['boards/hc7000/uj11_sram.v','boards/hc7000/video/uj11_video_arbiter.v',
             'tests/models/async_sram_model.v','tests/tb_sram50.v','tests/tb_pal_sram_fast.v']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources+['tools/test_sram_fast.py']}
    cases=[]
    for fast in (0,1):
        for pin,dq in ((0,0),(0,15),(10,0),(10,15)):
            name=f'sram-{fast}-{pin}-{dq}'
            cases.append((name,'tb_sram50',[f'-Ptb_sram50.FAST_RESPONSE={fast}',
                f'-Ptb_sram50.PIN_DELAY={pin}',f'-Ptb_sram50.DQ_DELAY={dq}']))
    cases.append(('arbiter','tb_pal_sram_fast',[]))
    for name,top,params in cases:
        subprocess.run(['iverilog','-g2012','-s',top,'-o',str(out/name)]+params+sources,cwd=ROOT,check=True)
        log=subprocess.check_output(['vvp',str(out/name)],cwd=ROOT,text=True)
        assert 'PASS' in log
        (out/(name+'.log')).write_text(log);print(log.splitlines()[0])
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    (out/'result.json').write_text(json.dumps(dict(passed=True,files=hashes,cases=[n for n,_,_ in cases]),indent=2)+'\n')


if __name__=='__main__':run()
