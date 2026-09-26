#!/usr/bin/env python3
"""Build MACRO-11 sources with the original RT-11 V5.03 MACRO/LINK in SIMH.

Only a private copy of the system disk is writable. No microasm11 changes.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import subprocess
import termios
import time
from board_common import ROOT


class Console:
    def __init__(self, ini, log):
        # SIMH configures ICANON/ECHO itself and uses VINTR for its break key.
        # Do not clear ISIG with tty.setraw before it starts.
        master, slave = pty.openpty()
        self.fd = master; self.log = log; self.buffer = b''
        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        self.process = subprocess.Popen(['pdp11', str(ini)], stdin=slave, stdout=slave, stderr=slave,
                                        preexec_fn=controlling_terminal)
        os.close(slave)

    def expect(self, pattern, timeout=60):
        deadline = time.monotonic()+timeout
        while True:
            match = re.search(pattern, self.buffer)
            if match:
                result = self.buffer[:match.end()]; self.buffer = self.buffer[match.end():]
                return result
            if time.monotonic() > deadline: raise TimeoutError((pattern, self.buffer[-1500:]))
            if not select.select([self.fd], [], [], 0.1)[0]: continue
            data = os.read(self.fd, 65536)
            if not data: raise RuntimeError('SIMH console closed')
            self.log.write(data); self.log.flush(); self.buffer += data

    def send(self, text):
        for byte in text.encode('ascii'):
            os.write(self.fd, bytes([byte])); time.sleep(0.002)

    def close(self):
        try:
            if self.process.poll() is None:
                self.send('\x05'); self.expect(rb'sim> ', timeout=5)
                # Simulator command mode uses a cooked terminal: LF terminates
                # its command even when inherited ICRNL was disabled.
                self.send('quit\n'); self.expect(rb'Goodbye', timeout=5)
                self.process.wait(timeout=5)
        finally:
            if self.process.poll() is None:
                self.process.kill(); self.process.wait(timeout=5)
            os.close(self.fd)


def build(sources, out, base, *, foreground=False):
    out.mkdir(parents=True, exist_ok=True)
    disk = out/'build.dsk'
    if disk.exists(): raise ValueError('Use a fresh build directory: '+str(out))
    digest = hashlib.sha256(base.read_bytes()).hexdigest(); shutil.copyfile(base, disk)
    rt = ROOT/'../lsi11/rt11tool'
    for src in sources:
        assert re.fullmatch('[A-Z][A-Z0-9]{0,5}', src.stem), src
        # RT-11 EDIT/MACRO source text uses CRLF.
        path = out/src.name
        path.write_bytes(src.read_text().replace('\r\n', '\n').replace('\n', '\r\n').encode('ascii'))
        subprocess.run([str(rt), 'add', str(disk), str(path), src.name], check=True, capture_output=True)
    ini = out/'build.ini'
    ini.write_text(f'set cpu 11/73\nset cpu 64k\nset clk 50hz\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
    with (out/'console.log').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'RT-11FB'); c.expect(rb'\r\n\.'); c.expect(rb'\r\n\.'); c.expect(rb'\r\n\.')
            c.send('SET SL OFF\r'); c.expect(rb'\r\n\.')
            for src in sources:
                name = src.stem
                c.send(f'R MACRO\r'); c.expect(rb'\*')
                c.send(f'{name},{name}={name}\r'); text = c.expect(rb'\*')
                assert b'Errors detected' not in text and b'Error' not in text and b'?MACRO' not in text, text
                c.send('\x03'); c.expect(rb'\r\n\.')
                c.send('R LINK\r'); c.expect(rb'\*')
                option = '/R' if foreground else ''
                c.send(f'{name}{option},{name}={name}\r'); text = c.expect(rb'\*')
                assert b'?LINK' not in text, text
                c.send('\x03'); c.expect(rb'\r\n\.')
        finally: c.close()
    results = {}
    for src in sources:
        for ext in ('REL' if foreground else 'SAV', 'OBJ', 'LST', 'MAP'):
            name = src.stem+'.'+ext
            subprocess.run([str(rt), 'extract', str(disk), str(out), name], check=True, capture_output=True)
            results[name] = hashlib.sha256((out/name).read_bytes()).hexdigest()
        listing = (out/(src.stem+'.LST')).read_text(errors='replace')
        assert re.search(r'Errors detected:\s+0\b', listing), listing[-3000:]
    assert hashlib.sha256(base.read_bytes()).hexdigest() == digest
    record = dict(base_sha256=digest, tool='RT-11 V5.03 MACRO/LINK in SIMH',
                  foreground=foreground,
                  source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                  outputs=results, driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (out/'build-inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='+', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--base', type=Path, default=ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    args = parser.parse_args()
    print(json.dumps(build([p.resolve() for p in args.sources], args.out.resolve(), args.base.resolve()), indent=2))
