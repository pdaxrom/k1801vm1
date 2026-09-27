#!/usr/bin/env python3
"""Program a source-verified HC7000 export and capture its first RT-11 boot."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from board_common import ROOT
from hardware_modules import UART

def run(args):
    build=ROOT/'build'/args.build
    report=json.loads((build/'result.json').read_text())
    exported=json.loads((build/'jed.json').read_text())
    assert report['device']=='LCMXO2-7000HC-4TG144C'
    assert report['timing_pass'] and report['fully_routed'] and report['diamond_returncode']==0
    for name,want in report['inputs']['files'].items():
        path=build/name.removeprefix('generated:') if name.startswith('generated:') else ROOT/name
        assert hashlib.sha256(path.read_bytes()).hexdigest()==want,path
    jed=Path(exported['jed'])
    assert hashlib.sha256(jed.read_bytes()).hexdigest()==exported['sha256']
    args.phase='program';u=UART(args)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    try:
        u.open();u.read(.3)
        u.record['jed_sha256']=exported['sha256'];u.save()
        # Release the open-drain JTAGENB jumper before Programmer owns FTDI A.
        subprocess.run([str(ROOT/'../lsi11-fpga/host/hg/hgfsd'),'--jtag-only'],check=True)
        xcf=args.out/'program.xcf'
        subprocess.run([sys.executable,str(ROOT/'tools/make_programmer_xcf.py'),
            str(jed),str(xcf),'--board','hc7000-lcd-sram'],check=True)
        diamond=Path(os.environ.get('DIAMOND_HOME',str(Path.home()/'.local/lscc/diamond/3.14')))
        boot_offset=len(u.capture)
        with (args.out/'programmer-stdout.log').open('w') as log:
            process=subprocess.Popen([str(diamond/'bin/lin64/pgrcmd'),'-infile',str(xcf),
                '-logfile',str(args.out/'programmer.log')],stdout=log,stderr=subprocess.STDOUT,
                env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')))
            while process.poll() is None:u.read(.1)
        u.record['programmer_exit_code']=process.returncode;u.save()
        assert process.returncode==0,(args.out/'programmer-stdout.log').read_text()
        assert 'Operation: successful' in (args.out/'programmer-stdout.log').read_text()
        deadline=time.monotonic()+90
        while b'RT-11FB' not in u.capture[boot_offset:]:
            if time.monotonic()>deadline:raise TimeoutError('No RT-11 boot after programming')
            u.read(.1)
        u.read(3);u.command('SET SL OFF')
        u.record['rt11_banner_seen']=True;u.record['passed']=True
    finally:u.close()
    print('\nPASS HC7000 programming and cold RT-11 boot')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int)
    a=p.parse_args();a.out=a.out.resolve();run(a)
