#!/usr/bin/env python3
"""Exercise CP67 FRAM module configuration on the Linux board host.

No FPGA programming. OFF and reinstallation are separate explicit phases;
each following test waits for a fresh user-operated cold RESET. Save the
disable session as --baseline for every subsequent phase.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import termios
import time


def parse_table(response):
    rows=re.findall(rb'(?m)^([0-7]): ([0-7]{6}) ([0-7]{6}) ([0-7]{6}) ([0-7]{6})\r?$',response)
    assert len(rows)==8 and [int(r[0]) for r in rows]==list(range(8)),response
    return [[int(v,8) for v in row[1:]] for row in rows]


class UART:
    def __init__(self,args):
        self.args=args;self.fd=None;self.old=None;self.paused=False
        self.capture=bytearray();self.log=None
        self.record=dict(phase=args.phase,port=args.port,passed=False,commands=[],
                         started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
                         driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())

    def save(self):
        self.record['uart_bytes']=len(self.capture)
        (self.args.out/'session.json').write_text(json.dumps(self.record,indent=2)+'\n')

    def open(self):
        self.args.out.mkdir(parents=True,exist_ok=True)
        assert not (self.args.out/'uart.bin').exists(),'fresh capture directory required'
        self.log=(self.args.out/'uart.bin').open('wb')
        pid=self.args.pause_pid
        if pid:
            links=[os.readlink(f) for f in Path(f'/proc/{pid}/fd').iterdir()]
            assert self.args.port in links,'selected reader does not hold this UART'
            assert Path(f'/proc/{pid}/stat').read_text().split(') ')[1][0] not in ('T','t')
            os.kill(pid,signal.SIGSTOP);self.paused=True
        self.fd=os.open(self.args.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        self.old=termios.tcgetattr(self.fd);attrs=termios.tcgetattr(self.fd)
        attrs[0]=0;attrs[1]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[3]=0
        attrs[4]=attrs[5]=termios.B115200;attrs[6][termios.VMIN]=0;attrs[6][termios.VTIME]=0
        termios.tcsetattr(self.fd,termios.TCSANOW,attrs)

    def read(self,seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if select.select([self.fd],[],[],min(.1,max(0,deadline-time.monotonic())))[0]:
                try:data=os.read(self.fd,4096)
                except BlockingIOError:continue
                if not data:raise RuntimeError('UART disconnected')
                self.capture.extend(data);self.log.write(data);self.log.flush()
                print(data.decode('ascii','backslashreplace'),end='',flush=True)

    def command(self,text,pattern=rb'\r\n\.',timeout=60):
        start=len(self.capture);began=time.monotonic()
        step=dict(command=text,offset=start,expected=pattern.decode('ascii'),complete=False)
        self.record['commands'].append(step)
        print('\n[SEND] '+text,flush=True)
        for byte in (text+'\r').encode('ascii'):
            os.write(self.fd,bytes([byte]));self.read(.1)
        deadline=time.monotonic()+timeout
        while True:
            response=bytes(self.capture[start:]);echo=response.find(text.encode('ascii'))
            if echo>=0 and re.search(pattern,response[echo+len(text):]):
                self.read(.3);response=bytes(self.capture[start:])
                step.update(complete=True,end=len(self.capture),response=response.decode('ascii','backslashreplace'),
                            elapsed_seconds=time.monotonic()-began)
                self.save();return response
            if time.monotonic()>deadline:raise TimeoutError('Missing response to '+repr(text))
            self.read(.1)

    def cold_boot(self):
        self.read(.3);start=len(self.capture);deadline=time.monotonic()+self.args.reset_timeout
        self.record['boot']=dict(offset=start,fresh_banner=False)
        self.save();print('[ARMED] Long RESET without ESC required now.',flush=True)
        while True:
            data=bytes(self.capture[start:]);banner=re.search(rb'RT-11FB[^\r\n]*V05\.03',data)
            if banner and len(re.findall(rb'\r\n\.',data[banner.end():]))>=3:
                self.read(1)
                self.record['boot'].update(fresh_banner=True,end=len(self.capture))
                self.save();self.command('SET SL OFF');return
            if time.monotonic()>deadline:raise TimeoutError('No fresh complete cold boot')
            self.read(.1)

    def module(self,text):
        self.command('RUN UJMOD',rb'UJMOD> ')
        return self.command(text)

    def table(self):return parse_table(self.module('STATUS'))

    def basic(self,name):
        self.command('RUN '+name,rb'INDIVIDUAL\)\?',90)
        self.command('A',rb'\r\nREADY\r\n',90)
        response=self.command('RUN B81TST',rb'\r\nREADY\r\n',600)
        assert b'CHECKS= 29  ERRORS= 0' in response and b'CP81 PASS' in response,response
        assert b'BAD' not in response and b'FAIL' not in response,response
        if name=='B81FPD':
            response=self.command('RUN B81DBL',rb'\r\nREADY\r\n',180)
            assert b'CP81 DOUBLE PASS' in response and b'FAIL' not in response,response
        errors=[('PRINT 1/0',b'?DIVISION BY ZERO'),('PRINT SQR(-1)',b'?NEGATIVE SQUARE ROOT'),
                ('PRINT 1E30*1E30',b'?FLOATING OVERFLOW'),('PRINT 2+2',b'\r\n 4 \r\n')]
        if name in ('B81FIS','B81FIJ'):
            errors += [('PRINT 1E-30*1E-30',b'?FLOATING UNDERFLOW'),
                       ('PRINT 1/0',b'?DIVISION BY ZERO'),('PRINT 2+2',b'\r\n 4 \r\n')]
        for command,expected in errors:
            response=self.command(command,rb'\r\nREADY\r\n',90)
            assert expected in response,response
        self.command('BYE')

    def close(self):
        try:
            if self.fd is not None:
                try:
                    if self.old is not None and self.paused:termios.tcsetattr(self.fd,termios.TCSANOW,self.old)
                finally:os.close(self.fd)
        finally:
            try:
                if self.paused:os.kill(self.args.pause_pid,signal.SIGCONT)
            finally:
                if self.log is not None:
                    self.log.close()
                    self.record['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                    self.save()


def run(args):
    u=UART(args)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    baseline=None
    if args.baseline:
        baseline=json.loads(args.baseline.read_text())
        assert baseline['phase']=='disable' and baseline['passed']
    if args.phase!='disable':assert baseline is not None,'disable session baseline required'
    try:
        u.open()
        if args.phase in ('fis-only','combined'):u.cold_boot()
        else:u.read(1);u.command('')
        before=u.table();u.record['before']=before
        if args.phase=='disable':
            slots=[i for i,r in enumerate(before) if r[:3]==[0o60000,0o4174,0o142034]]
            assert len(slots)==1,'expected installed CP80 FPP not found'
            slot=slots[0];assert before[slot][3]==0o140407
            assert sum(r[3]==0o140407 for r in before)==3
            u.record['slot']=slot;u.save()
            response=u.module('OFF'+str(slot));assert b'!UJMOD-I-DONE' in response,response
            expected=[r.copy() for r in before];expected[slot][3]=0o140401
        else:
            slot=baseline['slot'];initial=baseline['before']
            expected=[r.copy() for r in initial]
            if args.phase!='combined':expected[slot][3]=0o140401
            assert before==expected,(before,expected)
            if args.phase=='restore':
                response=u.module('FP11');assert b'!UJMOD-I-DONE' in response,response
                expected[slot][3]=0o140403
            else:
                for name in (['B81FIS','B81FIJ'] if args.phase=='fis-only' else ['B81FIJ','B81FPU','B81FPD']):
                    u.basic(name)
        after=u.table();u.record['after']=after
        assert after==expected,(after,expected)
        u.record['passed']=True
    finally:u.close()
    print('\nPASS CP82 '+args.phase,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--phase',required=True,choices=['disable','fis-only','restore','combined'])
    p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path)
    p.add_argument('--port',default='/dev/ttyUSB1');p.add_argument('--pause-pid',type=int)
    p.add_argument('--reset-timeout',type=float,default=3600)
    run(p.parse_args())
