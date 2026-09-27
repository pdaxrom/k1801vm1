#!/usr/bin/env python3
"""Qualify HGX on HC7000 via the existing FB handler, then return to XM.

Run from the MMU checkout on the Linux bench. Only an absent HGX.SYS and
explicitly checked temporary test files are written on the board SD.
The HG host volume must be a fresh directory dedicated to this test.
"""
import argparse
import hashlib
import json
import re
import signal
import subprocess
import time
from pathlib import Path
from hardware_modules import UART


def run(a):
    a.out.mkdir(parents=True,exist_ok=False)
    volume=a.out/'volume';volume.mkdir()
    source=a.hgx.read_bytes();(volume/'HGX.SYS').write_bytes(source)
    payload=bytes(((i*29)^(i>>8)^0xa5)&255 for i in range(17021))
    (volume/'ROUND.BIN').write_bytes(payload)
    (volume/'HELLO.TXT').write_bytes(b'HC7000 XM HG MAPPED TRANSFER\r\n')
    u=UART(a);daemon=None
    def checked(text,**kwargs):
        reply=u.command(text,**kwargs)
        assert b'?' not in reply.replace(b'?BINCOM-I-No differences found',b''),reply
        return reply
    def received(name,expected_bytes):
        path=volume/name;deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            if path.exists() and path.stat().st_size==expected_bytes:return path.read_bytes()
            u.read(.2)
        raise TimeoutError('host directory export not completed: '+name)
    def boot(name):
        checked('BOOT '+name,pattern=rb'RT-11'+(b'FB' if name=='RT11FB' else b'XM')+rb'[^\r\n]*V05\.03')
        u.read(4);checked('SET SL OFF')
    def stop(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        u.open();u.read(.3);checked('SET SL OFF')
        config=checked('SHOW CONFIGURATION')
        assert b'Booted from DM0:RT11XM' in config or (a.resume_install and b'Booted from DM0:RT11FB' in config)
        if not a.resume_install:
            assert b'0 Files, 0 Blocks' in checked('DIR SY:HGX.SYS'),'HGX.SYS already exists; do not overwrite'
        with (a.out/'hgfsd.log').open('w') as log:
            daemon=subprocess.Popen([str(a.hgfsd),'--directory',str(volume),'--jtag-enable-adbus7'],stdout=log,stderr=subprocess.STDOUT)
            time.sleep(1);assert daemon.poll() is None
            if b'Booted from DM0:RT11FB' not in config:boot('RT11FB')
            checked('LOAD HG')
            if not a.resume_install:
                reply=u.command('COPY HG:HGX.SYS SY:HGX.SYS',timeout=180)
                assert b'?' not in reply.replace(b'?PIP-W-Reboot',b''),reply
            assert b'No differences found' in checked('DIFFERENCES/BINARY HG:HGX.SYS SY:HGX.SYS',timeout=180)
            checked('COPY SY:HGTIME.SAV HG:OLDTIM.SAV',timeout=180)
            oldtime=received('OLDTIM.SAV',len(a.hgtime.read_bytes()))
            u.record['existing_hgtime_sha256']=hashlib.sha256(oldtime).hexdigest()
            assert oldtime==a.hgtime.read_bytes()
            checked('UNLOAD HG')
            boot('SY:')
            checked('LOAD HG')
            response=checked('RUN HGTIME',timeout=180)
            assert b'RT-11 SYSTEM DATE AND TIME SET FROM HOST' in response
            before=time.time();t=checked('TIME');d=checked('DATE');after=time.time()
            m=re.search(rb'\r\n(\d\d):(\d\d):(\d\d)',t);assert m,t
            seconds=sum(int(v)*n for v,n in zip(m.groups(),(3600,60,1)))
            host=time.localtime((before+after)/2);hostseconds=host.tm_hour*3600+host.tm_min*60+host.tm_sec
            delta=(seconds-hostseconds+43200)%86400-43200
            assert abs(delta)<6,(delta,t)
            assert f'{host.tm_mday}-{time.strftime("%b-%Y",host)}'.encode() in d,d
            u.record['host_time_delta_seconds']=delta
            assert b'HC7000 XM HG MAPPED TRANSFER' in checked('TYPE HG:HELLO.TXT')
            assert b'0 Files, 0 Blocks' in checked('DIR SY:XGTEST.BIN'),'scratch file exists'
            checked('COPY HG:ROUND.BIN SY:XGTEST.BIN',timeout=600)
            checked('COPY SY:XGTEST.BIN HG:RETURN.BIN',timeout=600)
            result=received('RETURN.BIN',17408)
            assert len(result)==17408 and result[:len(payload)]==payload and not any(result[len(payload):])
            u.record['roundtrip']=dict(payload_bytes=len(payload),rt11_bytes=len(result),
                sha256=hashlib.sha256(result).hexdigest(),mapped_monitor='RT11XM')
            checked('DELETE/NOQUERY SY:XGTEST.BIN')
            checked('SHOW MEMORY')
            checked('UNLOAD HG')
            daemon.send_signal(signal.SIGTERM);daemon.wait(timeout=10);assert daemon.returncode==0;daemon=None
        subprocess.run([str(a.hgfsd),'--jtag-only'],check=True)
        checked('LOAD HG');checked('UNLOAD HG')
        assert b'Booted from DM0:RT11XM' in checked('SHOW CONFIGURATION')
        u.record['hgx_sha256']=hashlib.sha256(source).hexdigest();u.record['passed']=True
    finally:
        if daemon is not None:
            daemon.send_signal(signal.SIGTERM);daemon.wait(timeout=10)
            subprocess.run([str(a.hgfsd),'--jtag-only'],check=True)
        u.close()
    print('PASS HGX: XM host time and 34-sector binary round trip',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hgx',type=Path,required=True);p.add_argument('--hgtime',type=Path,required=True)
    p.add_argument('--hgfsd',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--resume-install',action='store_true',help='verify a previously copied HGX before resuming; never overwrite it')
    p.add_argument('--pause-pid',type=int,required=True);p.add_argument('--port',default='/dev/ttyUSB1')
    a=p.parse_args();a.out=a.out.resolve();a.phase='hc7000-hgx-xm';run(a)
