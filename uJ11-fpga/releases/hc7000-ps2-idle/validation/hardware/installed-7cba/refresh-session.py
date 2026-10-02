#!/usr/bin/env python3
"""Single UART owner for the authorized HC7000 terminal flash; JSON stdin."""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import termios
from types import SimpleNamespace

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT / 'tools'))
from hardware_modules import UART

OUT = ROOT / 'build/ps2-idle-hardware-20261002/session-02'
APPROVED = ROOT / 'build/ps2-idle-hardware-20261002/approved.json'


class Port(UART):
    def open(self):
        users = subprocess.run(['fuser', self.args.port], capture_output=True, text=True)
        owners = {int(p) for p in users.stdout.split()}
        assert owners <= {self.args.pause_pid}, 'Unexpected UART owner'
        if not owners:
            self.args.pause_pid = None
        super().open()
        attrs = termios.tcgetattr(self.fd)
        attrs[0] |= termios.ISTRIP
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def send(self, data):
        for byte in data:
            assert os.write(self.fd, bytes([byte])) == 1
            self.read(.02)


def verify():
    approved = json.loads(APPROVED.read_text())
    build = ROOT / 'build' / approved['name']
    result = json.loads((build / 'result.json').read_text())
    export = json.loads((build / 'jed.json').read_text())
    assert result['timing_pass'] and result['fully_routed'] and result['trace_timing_pass']
    assert result['device'] == 'LCMXO2-7000HC-4TG144C'
    assert result['applied_clock_mhz'] == 50 and result['input_clock_mhz'] == 12
    assert result['inputs']['video'] and result['ebr'] == 26
    for name, digest in result['inputs']['files'].items():
        path = build / name.removeprefix('generated:') if name.startswith('generated:') else ROOT / name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, path
    for suffix, digest in result['reports'].items():
        path = build / 'impl1' / (build.name + '_impl1' + suffix)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, path
    jed = Path(export['jed'])
    assert hashlib.sha256(jed.read_bytes()).hexdigest() == export['sha256'] == approved['sha256']
    assert export['checksum'] == approved['checksum']
    return jed, approved


def program(u):
    jed, approved = verify()
    mode = u.record.get('preflash_mode')
    if mode == 'rt11':
        response = u.command('DIR', rb'\r\n\.', 45)
        assert response.endswith(b'\r\n.') and b'Files,' in response
        assert b'Free blocks' in response and b'?KMON' not in response
        u.record['preflash_state'] = 'RT-11 idle after fresh read-only DIR'
    elif mode == 'rsx_shutdown':
        last = u.record['commands'][-1]
        response = last.get('response', '')
        assert last['complete'] and 'SHUTUP operation complete' in response
        assert 'Dismount complete' in response and 'DU1:' in response
        assert last['end'] == len(u.capture), 'No further UART traffic expected after shutdown'
        u.read(1)
        assert last['end'] == len(u.capture), 'RSX must remain quiescent after shutdown'
        u.record['preflash_state'] = 'RSX SHUTUP complete; DU1 dismounted; quiescent UART'
    elif mode == 'bsd_bootstrap':
        start = len(u.capture)
        u.send(b'\r')
        u.read(2)
        response = bytes(u.capture[start:])
        assert b'xp(0,0)unix' in response and b'unix not found' in response
        assert response.endswith(b': '), 'Fresh idle 70Boot response required before flash'
        u.record['preflash_state'] = '70Boot idle; failed default XP lookup; no running OS'
    elif mode == 'odt':
        start = len(u.capture)
        u.send(b'R 7\r')
        u.read(.5)
        response = bytes(u.capture[start:])
        assert re.fullmatch(rb'[0-7]{6}\r\n>', response), 'Fresh ODT register response required before flash'
        u.record['preflash_pc_octal'] = response[:6].decode('ascii')
        u.record['preflash_state'] = 'CPU stopped in ODT'
    else:
        raise RuntimeError('A fresh idle RT-11 or stopped ODT state is required')
    u.save()
    subprocess.run(['/home/sash/Work/FPGA/uj11-hc7000-port-20260927/lsi11-fpga/host/hg/hgfsd',
                    '--jtag-only'], check=True)
    xcf = OUT / 'program.xcf'
    subprocess.run([sys.executable, 'tools/make_programmer_xcf.py', str(jed), str(xcf),
                    '--board', 'hc7000-lcd-sram'], check=True)
    xcf.write_text(xcf.read_text().replace('<Operation>FLASH Erase,Program,Verify</Operation>', '<Operation>SRAM Refresh</Operation>').replace('<AccessMode>FLASH</AccessMode>', '<AccessMode>SRAM</AccessMode>'))
    diamond = Path.home() / '.local/lscc/diamond/3.14'
    start = len(u.capture)
    cancelled = False

    def listen():
        nonlocal cancelled
        u.read(.05)
        if not cancelled and b'other key=menu:' in u.capture[start:]:
            u.send(b' ')
            cancelled = True

    with (OUT / 'programmer-stdout.log').open('w') as log:
        p = subprocess.Popen([str(diamond / 'bin/lin64/pgrcmd'), '-infile', str(xcf),
                              '-logfile', str(OUT / 'programmer.log')], stdout=log, stderr=subprocess.STDOUT,
                             env=dict(os.environ, LD_PRELOAD='/lib/x86_64-linux-gnu/libstdc++.so.6'))
        while p.poll() is None:
            listen()
    report = (OUT / 'programmer-stdout.log').read_text()
    print(report, flush=True)
    assert p.returncode == 0 and 'Operation: successful' in report, 'Program/verify failed'
    u.record.update(jed_sha256=approved['sha256'], refresh_success=True, build=approved['name'], jed_checksum=approved['checksum'])
    u.save()
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        listen()
        if b'Select controller [1/2/3/4/5/6]:' in u.capture[start:]:
            u.record.update(fresh_boot_menu=True, autoboot_cancelled=cancelled)
            u.save()
            print('\nREFRESHED HC7000 7CBA; fresh SD menu captured', flush=True)
            return
    raise TimeoutError('No fresh boot menu after programming')


def main():
    u = Port(SimpleNamespace(out=OUT, phase='HC7000 PS2 keyboard hardware',
                             port='/dev/ttyUSB1', pause_pid=223620))

    def interrupted(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        u.open()
        u.read(1)
        print('\nREADY; UART exclusively opened', flush=True)
        for line in sys.stdin:
            try:
                c = json.loads(line)
                action = c['action']
                if action == 'read':
                    u.read(min(float(c.get('seconds', 1)), 30))
                elif action == 'probe':
                    u.send(c['text'].encode('ascii'))
                    u.read(min(float(c.get('seconds', 1)), 30))
                elif action == 'command':
                    u.command(c['text'], c['pattern'].encode('ascii'), min(float(c.get('timeout', 30)), 60))
                elif action == 'refresh':
                    program(u)
                elif action == 'record':
                    u.record.update(c['data'])
                elif action == 'close':
                    break
                else:
                    raise ValueError(action)
                u.save()
                print('\nACTION COMPLETE ' + action, flush=True)
            except Exception as error:
                print('\nACTION FAILED ' + repr(error), flush=True)
                u.record.setdefault('errors', []).append(repr(error))
                u.save()
    finally:
        u.close()
        print('CLOSED; UART released', flush=True)


if __name__ == '__main__':
    main()
