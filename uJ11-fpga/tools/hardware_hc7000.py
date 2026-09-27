#!/usr/bin/env python3
"""Qualify the HC7000 port on the Linux bench using the existing SD programs.

Run modules after FPGA configuration (empty SRAM). Other phases retain RAM.
No reset button or FPGA programming is performed by this runner.
"""
import argparse
import re
import signal
import time
from pathlib import Path
from hardware_modules import UART


def run(args):
    u=UART(args)
    def interrupted(signum,frame): raise KeyboardInterrupt
    signal.signal(signal.SIGINT,interrupted)
    signal.signal(signal.SIGTERM,interrupted)
    try:
        u.open();u.read(.3);u.command('SET SL OFF')
        if args.phase=='modules':
            before=u.table()
            assert all(row==[0,0,0,0] for row in before),'expected newly cleared SRAM'
            for name in ('ODT','SDBOOT','FP11 AUTO'):
                assert b'!UJMOD-I-DONE' in u.module(name)
            u.record['installed']=u.table()
            response=u.command('RUN UJBOOT',rb'RT-11FB[^\r\n]*V05\.03',90)
            u.read(3);u.command('SET SL OFF')
            table=u.table()
            assert [r[3] for r in table]==[0o140407]*3+[0]*5,table
            u.record['initialized']=table
        elif args.phase=='basic':
            for name in ('B81FIJ','B81FPU','B81FPD'):u.basic(name)
        elif args.phase=='odt':
            u.command('RUN ODTCHK',rb'ODT> ')
            assert b'AC2=040200 012345 067012 034567' in u.command('F 2',rb'ODT> ')
            before=u.command('R',rb'ODT> ')
            after=u.command('S',rb'ODT> ')
            pc=lambda b:re.search(rb'R7=([0-7]{6})',b)[1]
            assert pc(before)!=pc(after),'STEP did not advance PC'
            response=u.command('C')
            assert b'!ODTCHK-I-RETURNED TO RT11' in response
        elif args.phase=='hg':
            u.command('LOAD HG')
            response=u.command('RUN HGTIME')
            assert b'!HGTIME-I-RT-11 SYSTEM DATE AND TIME SET FROM HOST' in response
            response=u.command('TIME')
            initial=time.monotonic()
            clock=lambda b:re.search(rb'\r\n(\d\d):(\d\d):(\d\d)',b)
            a=clock(response);assert a,response
            u.read(10)
            b=clock(u.command('TIME'));assert b
            seconds=lambda m:sum(int(v)*k for v,k in zip(m.groups(),(3600,60,1)))
            delta=(seconds(b)-seconds(a))%86400
            elapsed=time.monotonic()-initial
            assert abs(delta-elapsed)<2,(delta,elapsed)
            u.record['clock_check']=dict(rt11_seconds=delta,host_seconds=elapsed)
            u.command('DIR HG:')
            response=u.command('DIR SY:U7TEST.TXT')
            assert b'0 Files, 0 Blocks' in response,'scratch file already exists'
            u.command('COPY HG:HELLO.TXT SY:U7TEST.TXT')
            assert b'HC7000 SD SRAM HG ROUNDTRIP' in u.command('TYPE SY:U7TEST.TXT')
            u.command('COPY SY:U7TEST.TXT HG:BACK.TXT')
            u.command('DELETE/NOQUERY SY:U7TEST.TXT')
            u.command('UNLOAD HG')
        u.record['passed']=True
    finally:u.close()
    print('\nPASS HC7000 '+args.phase,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--phase',choices=['modules','basic','odt','hg'],required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int)
    run(p.parse_args())
