#!/usr/bin/env python3
"""Program the HC7000 MMU export or execute an audited RT-11 UART session.

Programming is explicit. The --monitor check requires a fresh boot banner;
--command operations wait for their own echo and a subsequent prompt.
The selected existing UART reader is restored even on failure.
"""
import argparse,hashlib,json,os,re,signal,subprocess,sys,time
from pathlib import Path
from board_common import ROOT
from hardware_modules import UART

def program(u,a):
    build=ROOT/'build'/a.build
    report=json.loads((build/'result.json').read_text())
    exported=json.loads((build/'jed.json').read_text())
    assert report['mmu'] and report['device']=='LCMXO2-7000HC-4TG144C'
    assert report['timing_pass'] and report['fully_routed'] and report['diamond_returncode']==0
    assert report['applied_clock_mhz']==24
    for name,want in report['inputs']['files'].items():
        path=build/name.removeprefix('generated:') if name.startswith('generated:') else ROOT/name
        assert hashlib.sha256(path.read_bytes()).hexdigest()==want,path
    prefix=build/'impl1'/(a.build+'_impl1')
    for suffix,want in report['reports'].items():
        assert hashlib.sha256(prefix.with_suffix(suffix).read_bytes()).hexdigest()==want,suffix
    jed=Path(exported['jed'])
    assert hashlib.sha256(jed.read_bytes()).hexdigest()==exported['sha256']
    u.record['jed_sha256']=exported['sha256'];u.save()
    assert a.hgfsd and a.hgfsd.is_file(),'--hgfsd is required to release JTAGENB'
    subprocess.run([str(a.hgfsd),'--jtag-only'],check=True)
    xcf=a.out/'program.xcf'
    subprocess.run([sys.executable,str(ROOT/'tools/make_programmer_xcf.py'),str(jed),str(xcf),'--board','hc7000-lcd-sram'],check=True)
    diamond=Path(os.environ.get('DIAMOND_HOME',str(Path.home()/'.local/lscc/diamond/3.14')))
    offset=len(u.capture)
    with (a.out/'programmer-stdout.log').open('w') as log:
        process=subprocess.Popen([str(diamond/'bin/lin64/pgrcmd'),'-infile',str(xcf),'-logfile',str(a.out/'programmer.log')],stdout=log,stderr=subprocess.STDOUT,
            env=dict(os.environ,LD_PRELOAD=os.environ.get('DIAMOND_LIBSTDCPP','/lib/x86_64-linux-gnu/libstdc++.so.6')))
        while process.poll() is None:u.read(.1)
    u.record['programmer_exit_code']=process.returncode;u.save()
    assert process.returncode==0,(a.out/'programmer-stdout.log').read_text()
    assert 'Operation: successful' in (a.out/'programmer-stdout.log').read_text()
    deadline=time.monotonic()+90
    while not re.search(rb'RT-11'+a.monitor.encode()+rb'[^\r\n]*V05\.03',u.capture[offset:]):
        if time.monotonic()>deadline:raise TimeoutError('No fresh RT-11 '+a.monitor+' banner')
        u.read(.1)
    u.record['fresh_boot_monitor']=a.monitor
    if a.boot_ready:
        ready=a.boot_ready.encode('ascii');deadline=time.monotonic()+a.command_timeout
        while True:
            data=bytes(u.capture[offset:]);marker=data.find(ready)
            if marker>=0 and re.search(rb'\r\n\.',data[marker+len(ready):]):break
            if time.monotonic()>deadline:raise TimeoutError('No startup completion marker/prompt: '+a.boot_ready)
            u.read(.1)
        u.record['boot_ready']=a.boot_ready;u.read(.3)
    else:u.read(3)
    u.save();u.command('SET SL OFF')

def run(a):
    u=UART(a)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    try:
        u.open();u.read(.5)
        if a.build:program(u,a)
        for command in a.command:
            response=u.command(command,timeout=a.command_timeout)
            if b'?' in response.replace(b'?BINCOM-I-No differences found',b''):raise RuntimeError('RT-11 reported an error; inspect capture')
        for basic in a.basic:u.basic(basic)
        u.record['passed']=True
    finally:u.close()
    print('\nPASS MMU board '+a.phase,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build');p.add_argument('--monitor',choices=('FB','XM'),default='XM')
    p.add_argument('--boot-ready',help='wait for this startup completion text and a following prompt before sending commands')
    p.add_argument('--hgfsd',type=Path);p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--phase',required=True);p.add_argument('--command',action='append',default=[])
    p.add_argument('--command-timeout',type=float,default=90)
    p.add_argument('--basic',action='append',choices=('B81FIJ','B81FPU','B81FPD'),default=[])
    a=p.parse_args();a.out=a.out.resolve();run(a)
