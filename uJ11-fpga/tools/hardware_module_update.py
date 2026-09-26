#!/usr/bin/env python3
"""Install/check software module v4 on the Linux host; never program the FPGA.

An install phase only updates backed-up SD files. Activate is a separate phase
after RTL tests; UJBOOT performs software startup, not a physical RESET test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
from hardware_modules import UART
from module_image import decode, placed_image, checksum


def run(a):
    d=a.directory.resolve();a.out=d/a.phase;u=UART(a);host=None
    manifest=json.loads((d/'manifest.json').read_text())
    names=('UJMOD.SAV','UJBOOT.SAV','ODT.BIN','SDBOOT.BIN','FP11.BIN')
    for name in names:
        assert hashlib.sha256((d/'share'/name).read_bytes()).hexdigest()==manifest['files'][name]
    def command(text,timeout=60):
        result=u.command(text,timeout=timeout);assert b'?' not in result,result;return result
    def module(text):
        result=u.module(text);assert b'!UJMOD-I-DONE' in result and b'?' not in result,result
    def reboot():
        offset=len(u.capture)
        u.command('RUN UJBOOT',rb'RT-11FB[^\r\n]*V05\.03',90)
        deadline=time.monotonic()+90
        while len(re.findall(rb'\r\n\.',bytes(u.capture[offset:])))<3:
            if time.monotonic()>deadline:raise TimeoutError('software boot did not finish')
            u.read(.2)
        u.read(.5);command('SET SL OFF')
    def debug_test():
        status=u.command('F 6',rb'ODT> ')
        match=re.search(rb'FPS=([0-7]{6}) MODE=D,[IL]',status)
        assert match and int(match[1],8)&0o200,status
        u.record['debug_fps']=int(match[1],8)
        pattern=b'AC2=040200 012345 067012 034567'
        assert pattern in u.command('F 2',rb'ODT> ')
        start=len(u.capture);burst=b'R 0'+b' '*600+b'\r'
        assert os.write(u.fd,burst)==len(burst)
        u.read(3)
        if b'ODT> ' not in u.capture[start:]:
            os.write(u.fd,b'\r');u.read(3)
        response=bytes(u.capture[start:])
        assert b'?INPUT OVERFLOW' in response and b'ODT> ' in response,response
        assert b'R4=000000' in u.command('R 4',rb'ODT> ')
        assert pattern in u.command('F 2',rb'ODT> ')
        response=command('C');assert b'!ODTCHK-I-RETURNED TO RT11' in response
        u.record['uart_burst']=dict(bytes=len(burst),baud=115200,overflow_reported=True,recovered=True,fp_state_preserved=True)
    def restore_clock():
        nonlocal host
        with (d/('resync-'+a.phase+'.log')).open('wb') as log:
            host=subprocess.Popen([str(a.hg.resolve()),'--directory',str(d/'qa-share'),'--clock','1000'],stdout=log,stderr=subprocess.STDOUT)
        u.read(2);assert host.poll() is None
        command('LOAD HG');command('RUN HGTIME')
        u.record['date']=command('DATE').decode();u.record['time']=command('TIME').decode()
        command('UNLOAD HG');host.send_signal(signal.SIGINT);host.wait(timeout=10);host=None
        command('FRUN SY:CLOCK');os.write(u.fd,b'\x02');u.read(.5)
        memory=command('SHOW MEMORY');assert b'CLOCK' in memory
        u.record.update(clock_foreground_running=True,console_background=True)
    try:
        u.open();u.read(1)
        if a.phase=='resume-odt':
            prior=json.loads((d/'activate/session.json').read_text())
            assert 'initialized' in prior
            debug_test()
            after=u.table();assert after==prior['initialized'],(after,prior['initialized'])
            restore_clock()
            u.record.update(after=after,passed=True,resumes='activate/session.json',
                correction='SETD preserves previous FPS bits; check D mode and AC contents')
            print('\nPASS module update resume-odt',flush=True)
            return
        command('SET SL OFF')
        before=u.table();u.record['before']=before;u.save()
        if a.phase=='install':
            command('UNLOAD HG')
            with (d/'hg.log').open('wb') as log:
                host=subprocess.Popen([str(a.hg.resolve()),'--directory',str(d/'share'),'--clock','1000'],stdout=log,stderr=subprocess.STDOUT)
            u.record['hg_pid']=host.pid;u.save();u.read(2)
            assert host.poll() is None,(d/'hg.log').read_text()
            command('LOAD HG')
            for name,backup in [('UJMOD.SAV','UM0926.SAV'),('ODT.BIN','OD0926.BIN'),
                                ('SDBOOT.BIN','SD0926.BIN'),('FP11.BIN','FP0926.BIN')]:
                response=u.command('DIR SY:'+backup)
                assert b'0 Files' in response or b'No files' in response,'refusing to overwrite backup'
                command('COPY SY:'+name+' SY:'+backup)
            for name in names:command('COPY HG:'+name+' SY:'+name,360)
            for name,target in zip(names,('CHMOD.SAV','CHBOOT.SAV','CHODT.BIN','CHSD.BIN','CHFP.BIN')):
                command('COPY SY:'+name+' HG:'+target,360)
                deadline=time.monotonic()+30
                while True:
                    p=d/'share'/target
                    actual=hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
                    if actual==manifest['files'][name]:break
                    if time.monotonic()>deadline:raise RuntimeError(('readback mismatch',name,actual))
                    u.read(.2)
                u.record.setdefault('readback_sha256',{})[name]=actual;u.save()
            module('ON0') # checksum-verified no-op on the running legacy ODT
            assert u.table()==before,'install/ON0 changed retained state'
            response=u.module('OFF7');assert b'?UJMOD-E-' in response
            assert u.table()==before,'malformed OFF changed directory'
            command('UNLOAD HG');host.send_signal(signal.SIGINT);host.wait(timeout=10);host=None
        else:
            install=json.loads((d/'install/session.json').read_text());assert install['passed']
            assert before==install['before'],'board state changed since staging'
            for name in ('ODT','SDBOOT'):module(name)
            module('DEL2');module('FP11 AUTO')
            table=u.table();u.record['staged']=table
            preferred=[decode((d/'share'/n).read_bytes()) for n in ('ODT.BIN','SDBOOT.BIN','FP11.BIN')]
            fpbase=preferred[0]['base']+preferred[0]['memory_bytes']
            for i,(name,base) in enumerate(zip(('ODT.BIN','SDBOOT.BIN','FP11.BIN'),(0o10000,0o6000,fpbase))):
                image=placed_image((d/'share'/name).read_bytes(),base)
                assert table[i]==[base,len(image)//2,checksum(image),0xc103],(i,table[i])
            reboot()
            table=u.table();assert all(row[3]==0xc107 for row in table[:3]),table
            u.record['initialized']=table;u.save()
            # Exact same software cold path, exercising ON without SD reload.
            module('OFF2');reboot()
            off=u.table();assert off[2][3]==0xc101 and off[0][3]==0xc107
            u.basic('B81FIS')
            module('ON2');reboot()
            for name in ('B81FIJ','B81FPU','B81FPD'):u.basic(name)
            after=u.table();assert after==table,(after,table)
            with (d/'hg-time.log').open('wb') as log:
                host=subprocess.Popen([str(a.hg.resolve()),'--directory',str(d/'qa-share'),'--clock','1000'],stdout=log,stderr=subprocess.STDOUT)
            u.read(2);assert host.poll() is None
            command('LOAD HG')
            command('COPY HG:ODTCHK.SAV SY:ODTCHK.SAV',180)
            command('COPY SY:ODTCHK.SAV HG:CHKODT.SAV',180)
            qa=d/'qa-share';deadline=time.monotonic()+30
            while not (qa/'CHKODT.SAV').exists():
                if time.monotonic()>deadline:raise TimeoutError('ODTCHK readback missing')
                u.read(.2)
            assert (qa/'CHKODT.SAV').read_bytes()==(qa/'ODTCHK.SAV').read_bytes()
            command('RUN HGTIME')
            u.record['date']=command('DATE').decode()
            u.record['time']=command('TIME').decode()
            command('UNLOAD HG');host.send_signal(signal.SIGINT);host.wait(timeout=10);host=None
            u.command('RUN ODTCHK',rb'ODT> ')
            debug_test()
            restore_clock()
            u.record.update(after=after,restart_kind='software; physical RESET not exercised')
        u.record['passed']=True
    finally:
        if host is not None and host.poll() is None:
            u.record['hg_left_running_after_error']=host.pid
        u.close()
    print('\nPASS module update '+a.phase,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--phase',choices=('install','activate','resume-odt'),required=True)
    p.add_argument('--hg',type=Path)
    p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int)
    run(p.parse_args())
