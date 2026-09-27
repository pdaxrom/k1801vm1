#!/usr/bin/env python3
"""Reference 2.9BSD multiuser boot on SIMH 11/70, 2 MiB, without FPP."""
import argparse
import json
from pathlib import Path
import shutil
import time
from board_common import ROOT
from build_sd_bsd import INPUTS, digest
from rt11_build import Console

def run(out):
    out=out.resolve();out.mkdir(parents=True,exist_ok=False)
    source=ROOT.parent/'lsi11/disks/bsd2.9'
    hashes={f:digest(source/f) for _,_,f,_ in INPUTS}
    for f in hashes:shutil.copyfile(source/f,out/f)
    ini=out/'test.ini'
    ini.write_text(f'''set cpu 11/70
set cpu 2M
set cpu nofpp
set clk 50hz
set tti 7b
set tto 7b
set rl0 rl02
set rl1 rl02
set rl2 dis
set rl3 dis
attach rl0 {out}/2.9BSD-root.rl02
attach rl1 {out}/swap.rl02
set rp0 rm05
attach rp0 {out}/2.9BSD-usr.rm05
set hk dis
set rk dis
set rq dis
set tq dis
set xq dis
set tm dis
set dz dis
set lp dis
show cpu
boot rl0
''')
    replies=[]
    with (out/'uart.txt').open('wb') as log:
        c=Console(ini,log)
        try:
            c.expect(rb'70Boot[\s\x7f]*: ',60);c.send('rl(0,0)rlunix\r')
            boot=c.expect(rb'# ',90)
            assert b'mem = 1979072' in boot and b'rl 0 csr 174400' in boot and b'xp 0 csr 176700' in boot
            c.send('\x04');boot=c.expect(rb'login: ',90);assert b'Mounted /usr' in boot
            time.sleep(1);c.send('root\r');boot=c.expect(rb'# ',60);assert b'Welcome to the 2.9BSD' in boot
            for cmd,want in [('ls /usr',b'bin'),('cat /etc/fstab',b'/dev/rl1:swap'),
                ('echo SERV-RL-WRITE > /tmp/serv-test',b''),('cat /tmp/serv-test',b'SERV-RL-WRITE'),
                ('echo SERV-RP-WRITE > /usr/tmp/serv-test',b''),('cat /usr/tmp/serv-test',b'SERV-RP-WRITE'),
                ('rm /tmp/serv-test /usr/tmp/serv-test',b''),('sync',b'')]:
                c.send(cmd+'\r');reply=c.expect(rb'# ',60)
                assert want in reply and b'panic:' not in reply
                replies.append(dict(command=cmd,output=reply.decode('ascii','replace')))
        finally:c.close()
    assert all(digest(source/f)==h for f,h in hashes.items())
    record=dict(passed=True,cpu='11/70',memory_bytes=2097152,fpp=False,inputs=hashes,
                multiuser=True,login='root',commands=replies,originals_unchanged=True)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS SIMH BSD: 70Boot, Ctrl+D, root login, RL/RP reads and writes, no FPP')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=ROOT/'build/test-bsd-simh')
    run(p.parse_args().out)
