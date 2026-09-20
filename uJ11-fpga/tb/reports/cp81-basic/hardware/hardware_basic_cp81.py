#!/usr/bin/env python3
"""Run installed CP81 BASIC tests on the Linux board host; no installation.

Temporarily pause only the selected UART reader and restore it in finally.
Each command requires its own echo and prompt before another command is sent.
"""
import argparse
import json
import os
from pathlib import Path
import re
import select
import signal
import termios
import time


def run(out, port, pause_pid, variants=None):
    out.mkdir(parents=True, exist_ok=True)
    if (out/'uart.bin').exists() or (out/'session.json').exists():
        raise ValueError('Use a fresh capture directory')
    fd=None;old=None;paused=False;capture=bytearray();commands=[]
    variants=variants or ['B81FIS','B81FPU','B81FPD']
    record=dict(port=port,variants=variants,started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),commands=commands,passed=False)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    with (out/'uart.bin').open('wb') as log:
        def read_for(seconds):
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                if select.select([fd],[],[],min(.1,max(0,deadline-time.monotonic())))[0]:
                    try:data=os.read(fd,4096)
                    except BlockingIOError:continue
                    if not data:raise RuntimeError('UART disconnected')
                    capture.extend(data);log.write(data);log.flush()
                    print(data.decode('ascii','backslashreplace'),end='',flush=True)
        def command(text,pattern,timeout=30):
            start=len(capture);began=time.monotonic()
            step=dict(command=text,offset=start,expected=pattern.decode('ascii'),complete=False)
            commands.append(step);print('\n[SEND] '+text,flush=True)
            for byte in (text+'\r').encode('ascii'):
                os.write(fd,bytes([byte]));read_for(.1)
            deadline=time.monotonic()+timeout
            while True:
                response=bytes(capture[start:]);echoed=response.find(text.encode('ascii'))
                if echoed>=0 and re.search(pattern,response[echoed+len(text):]):
                    read_for(.3);response=bytes(capture[start:])
                    step.update(complete=True,elapsed_seconds=time.monotonic()-began,end=len(capture),
                                response=response.decode('ascii','backslashreplace'))
                    return response
                if time.monotonic()>deadline:raise TimeoutError('Missing prompt after '+repr(text))
                read_for(.1)
        def modules():
            command('RUN UJMOD',rb'UJMOD> ')
            response=command('STATUS',rb'\r\n\.')
            assert response.count(b'140407')==3,response
        try:
            if pause_pid:
                links=[os.readlink(f) for f in Path(f'/proc/{pause_pid}/fd').iterdir()]
                assert port in links,'selected process does not hold this UART'
                state=Path(f'/proc/{pause_pid}/stat').read_text().split(') ')[1][0]
                assert state not in ('T','t'),'reader already paused'
                os.kill(pause_pid,signal.SIGSTOP);paused=True
            fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            old=termios.tcgetattr(fd);attrs=termios.tcgetattr(fd)
            attrs[0]=0;attrs[1]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[3]=0
            attrs[4]=attrs[5]=termios.B115200;attrs[6][termios.VMIN]=0;attrs[6][termios.VTIME]=0
            termios.tcsetattr(fd,termios.TCSANOW,attrs);read_for(1)
            command('',rb'\r\n\.')
            modules()
            for name in variants:
                command('RUN '+name,rb'INDIVIDUAL\)\?',90)
                command('A',rb'\r\nREADY\r\n',90)
                response=command('RUN B81TST',rb'\r\nREADY\r\n',600)
                assert b'CHECKS= 29  ERRORS= 0' in response and b'CP81 PASS' in response,response
                assert b'BAD' not in response and b'FAIL' not in response,response
                if name=='B81FPD':
                    response=command('RUN B81DBL',rb'\r\nREADY\r\n',180)
                    assert b'CP81 DOUBLE PASS' in response and b'FAIL' not in response,response
                for text,expected in (
                    ('PRINT 1/0',b'?DIVISION BY ZERO'),
                    ('PRINT SQR(-1)',b'?NEGATIVE SQUARE ROOT'),
                    ('PRINT 1E30*1E30',b'?FLOATING OVERFLOW'),
                    ('PRINT 2+2',b'\r\n 4 \r\n')):
                    response=command(text,rb'\r\nREADY\r\n',90)
                    assert expected in response,response
                command('BYE',rb'\r\n\.')
            modules();record['passed']=True
        finally:
            try:
                if fd is not None:
                    try:
                        if old is not None and paused:termios.tcsetattr(fd,termios.TCSANOW,old)
                    finally:os.close(fd)
            finally:
                try:
                    if paused:
                        try:os.kill(pause_pid,signal.SIGCONT)
                        except ProcessLookupError:record['reader_exited']=True
                finally:
                    record.update(uart_bytes=len(capture),finished_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()))
                    (out/'session.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int)
    p.add_argument('--variant',action='append',choices=['B81FIS','B81FPU','B81FPD'])
    a=p.parse_args();run(a.out,a.port,a.pause_pid,a.variant)
