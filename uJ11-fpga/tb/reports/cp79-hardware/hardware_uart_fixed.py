#!/usr/bin/env python3
"""Capture HC1200 UART around an explicitly requested Programmer operation.

Run on the Linux board host. Temporarily pause only the selected UART reader;
restore its termios and resume it in finally. An unowned UART stays raw 115200
to avoid echoing late device output back into RT-11. Commands use 10 chars/s.
"""
import argparse, json, os, re, select, signal, subprocess, termios, time
from pathlib import Path


def escape_until_boot(fd, capture, read_for, timeout, result):
    """Select CP67 ROM recovery; stop transmitting on a fresh RT-11 banner.

    Already captured text cannot complete a new attempt. In particular, neither
    ESC echo nor an existing RT-11 prompt proves that cold reset took place.
    On timeout the caller's finally still restores the UART reader.
    """
    start = len(capture)
    began = time.monotonic()
    result.update(escape_bytes=0, fresh_rt11_banner=False)
    print('[ARMED] UART ESC recovery: perform a long RESET now.', flush=True)
    try:
        while True:
            remaining = timeout - (time.monotonic() - began)
            if remaining <= 0:
                raise RuntimeError('Recovery timed out without a fresh RT-11FB V05.03 banner; no further commands sent')
            if os.write(fd, b'\x1b') != 1:
                raise RuntimeError('UART ESC write was not completed')
            result['escape_bytes'] += 1
            read_for(min(.1, remaining))
            if re.search(rb'RT-11FB[^\r\n]*V05\.03', capture[start:]):
                result['fresh_rt11_banner'] = True
                print('\n[BOOT] Fresh RT-11 banner; UART ESC transmission stopped.', flush=True)
                return
    finally:
        result['elapsed_seconds'] = time.monotonic() - began


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='/dev/ttyUSB1')
    p.add_argument('--pause-pid',type=int)
    p.add_argument('--xcf',type=Path)
    p.add_argument('--interrupt',action='store_true',help='send two Ctrl-C bytes before commands')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--command',action='append',default=[])
    p.add_argument('--command-wait',type=float,default=20)
    p.add_argument('--listen',type=float,default=1,help='initial passive capture seconds without programming')
    p.add_argument('--expect-prompt',action='store_true',help='require a returned RT-11 prompt after each command')
    p.add_argument('--expect-odt-prompt',action='store_true',help='require a returned ODT prompt after each command')
    p.add_argument('--escape-until-boot',type=float,metavar='SECONDS',
                   help='CP67 recovery: repeat UART ESC until a fresh RT-11FB V05.03 banner, then listen passively')
    a=p.parse_args()
    if a.expect_prompt and a.expect_odt_prompt:
        p.error('select either RT-11 or ODT prompt, not both')
    if a.escape_until_boot is not None:
        if not 0 < a.escape_until_boot <= 600:
            p.error('--escape-until-boot requires 0 < SECONDS <= 600')
        if a.xcf or a.interrupt or a.command or a.expect_prompt or a.expect_odt_prompt:
            p.error('--escape-until-boot is a capture-only operation; do not combine with programming or commands')
    a.out.mkdir(parents=True,exist_ok=True)
    fd=None;old=None;paused=False;capture=bytearray()
    record=dict(port=a.port,started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),commands=a.command)
    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted)
    signal.signal(signal.SIGINT,interrupted)
    with (a.out/'uart.bin').open('wb') as log:
        def read_for(seconds):
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                ready,_,_=select.select([fd],[],[],min(.1,max(0,deadline-time.monotonic())))
                if ready:
                    try:data=os.read(fd,4096)
                    except BlockingIOError:continue
                    if not data:raise RuntimeError('UART disconnected')
                    capture.extend(data);log.write(data);log.flush()
                    print(data.decode('ascii','backslashreplace'),end='',flush=True)
        try:
            if a.pause_pid:
                links=[os.readlink(f) for f in Path(f'/proc/{a.pause_pid}/fd').iterdir()]
                assert a.port in links,'selected process does not hold this UART'
                state=Path(f'/proc/{a.pause_pid}/stat').read_text().split(') ')[1][0]
                assert state not in ('T','t'),'reader already paused; do not change its state'
                os.kill(a.pause_pid,signal.SIGSTOP);paused=True
            fd=os.open(a.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            old=termios.tcgetattr(fd);attrs=termios.tcgetattr(fd)
            attrs[0]=0;attrs[1]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[3]=0
            attrs[4]=attrs[5]=termios.B115200
            attrs[6][termios.VMIN]=0;attrs[6][termios.VTIME]=0
            termios.tcsetattr(fd,termios.TCSANOW,attrs)
            read_for(.2)
            if a.xcf:
                programmer=Path.home()/'.local/lscc/programmer/diamond/3.14/bin/lin64/pgrcmd'
                with (a.out/'programmer-stdout.log').open('w') as plog:
                    proc=subprocess.Popen([str(programmer),'-infile',str(a.xcf.resolve()),'-logfile',str((a.out/'programmer.log').resolve())],stdout=plog,stderr=subprocess.STDOUT)
                    while proc.poll() is None:read_for(.25)
                    record['programmer_exit_code']=proc.returncode
                    assert proc.returncode==0,'Programmer failed; see programmer.log'
                read_for(30)
            elif a.escape_until_boot is not None:
                record['recovery_attempt']={}
                escape_until_boot(fd,capture,read_for,a.escape_until_boot,record['recovery_attempt'])
                read_for(a.listen)
            else:read_for(a.listen)
            if a.interrupt:
                os.write(fd,b'\x03');read_for(.2);os.write(fd,b'\x03');read_for(2)
            for command in a.command:
                start=len(capture)
                print('\n[SEND] '+command,flush=True)
                for byte in (command+'\r').encode('ascii'):
                    os.write(fd,bytes([byte]));read_for(.1)
                if a.expect_prompt or a.expect_odt_prompt:
                    prompt=b'ODT> ' if a.expect_odt_prompt else b'\n.'
                    deadline=time.monotonic()+a.command_wait
                    while True:
                        response=bytes(capture[start:])
                        echoed=response.find(command.encode('ascii'))
                        if echoed>=0 and prompt in response[echoed+len(command):]:
                            read_for(.3)
                            break
                        if time.monotonic()>=deadline:
                            raise RuntimeError('Command echo and following selected prompt required; do not send another command')
                        read_for(.25)
                else:
                    read_for(a.command_wait)
                    assert len(capture)>start,'No UART response; command completion is unverified'
            record['uart_bytes']=len(capture)
            record['rt11_banner_seen']=b'RT-11' in capture
            record['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        finally:
            try:
                if fd is not None:
                    try:
                        if old is not None and paused:termios.tcsetattr(fd,termios.TCSANOW,old)
                    finally:os.close(fd)
            finally:
                try:
                    if paused:
                        try:os.kill(a.pause_pid,signal.SIGCONT)
                        except ProcessLookupError:record['reader_exited']=True
                finally:
                    # Preserve the actual capture even if reading, programming,
                    # termios restoration or reader cleanup failed.
                    record['uart_bytes']=len(capture)
                    record['rt11_banner_seen']=b'RT-11' in capture
                    record['capture_closed_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
                    (a.out/'session.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
