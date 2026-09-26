#!/usr/bin/env python3
"""Measure RT-11 .GTIM ticks against Linux monotonic time using TMRATE.SAV.

Run on the board host after installing TMRATE.SAV. No clock settings are written.
--interrupt stops the current background program; --resume-clock starts CLOCK
after a successful measurement. UART reader state is restored in finally.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse,datetime,hashlib,json,os,re,time
from hardware_modules import UART
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--port',default='/dev/ttyUSB1')
p.add_argument('--pause-pid',type=int)
p.add_argument('--interrupt',action='store_true')
p.add_argument('--resume-clock',action='store_true')
p.add_argument('--timeout',type=float,default=180)
p.add_argument('--divisor',type=int,required=True,help='KW11 divisor in the programmed FPGA image')
a=p.parse_args()
if not 1<=a.divisor<=1048575:p.error('divisor outside the KW11 LFSR period')
u=UART(SimpleNamespace(phase='clock-ticks',out=a.out,port=a.port,pause_pid=a.pause_pid))
u.record['measurement_script_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
try:
    u.open();u.read(.3)
    if a.interrupt:
        os.write(u.fd,b'\x03');u.read(.3);os.write(u.fd,b'\x03');u.read(1)
    start=len(u.capture)
    for byte in b'RUN TMRATE':os.write(u.fd,bytes([byte]));u.read(.1)
    os.write(u.fd,b'\r')
    samples=[];position=start;began=time.monotonic();last=0;cfg=None;wrap=0;previous=None
    while time.monotonic()-began<a.timeout:
        u.read(.01)
        data=bytes(u.capture)
        cm=re.search(rb'CFG ([0-7]{6})',data[start:])
        if cm:cfg=int(cm[1],8)
        for m in re.finditer(rb'(?m)^T ([0-7]{6}) ([0-7]{6})\r\n',data[position:]):
            stamp=time.monotonic();ticks=int(m[1],8)*65536+int(m[2],8)
            assert cfg is not None,'configuration must precede snapshots'
            hz=50 if cfg&0o40 else 60
            if previous is not None and ticks<previous:
                assert previous-ticks>43200*hz,'system time changed during measurement'
                wrap+=86400*hz
            previous=ticks
            samples.append(dict(monotonic=stamp,ticks=ticks+wrap,wall=datetime.datetime.now().astimezone().isoformat()))
            last=position+m.end()
        if last:position=last
        if len(samples)>=121:
            u.read(.5);break
        if len(samples) and len(samples)%30==0 and u.record.get('reported')!=len(samples):
            u.record['reported']=len(samples);print('\nSAMPLES',len(samples),flush=True)
        u.record.update(samples=samples,configuration_word=cfg)
    assert len(samples)==121,len(samples)
    assert cfg is not None and cfg&0o100000,cfg
    hz=50 if cfg&0o40 else 60
    xs=[s['monotonic']-samples[0]['monotonic'] for s in samples];ys=[s['ticks']-samples[0]['ticks'] for s in samples]
    xm=sum(xs)/len(xs);ym=sum(ys)/len(ys)
    frequency=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/sum((x-xm)**2 for x in xs)
    assert all(y2>y1 for y1,y2 in zip(ys,ys[1:])),ys
    u.record.update(samples=samples,configuration_word=cfg,declared_hz=hz,ticks_per_host_second=frequency,
        rate=frequency/hz,percent=(frequency/hz-1)*100,host_interval=xs[-1],guest_ticks=ys[-1],
        divisor=a.divisor,inferred_oscillator_hz=frequency*a.divisor)
    assert b'\n.' in bytes(u.capture[position:]),'diagnostic did not return'
    if a.resume_clock:
        u.command('RUN CLOCK',rb'CLOCK: RT-11 TIME ON HDSP; UART Q OR ESC TO EXIT')
    u.record['passed']=True
finally:u.close()
print('\nRESULT '+json.dumps({k:u.record[k] for k in ('configuration_word','declared_hz','ticks_per_host_second','percent','host_interval','guest_ticks','inferred_oscillator_hz')}),flush=True)
