#!/usr/bin/env python3
"""Check the installed XM HG driver and an SD write/read round trip on SERV.

Uses a fresh host directory and a checked-absent SD scratch file. Does not
install handlers, reboot the guest, or reprogram the FPGA.
"""
import argparse
import hashlib
import re
import signal
import subprocess
import time
from pathlib import Path
from hardware_modules import UART


def run(a):
    a.out.mkdir(parents=True, exist_ok=False)
    volume = a.out / 'volume'
    volume.mkdir()
    payload = bytes(((i * 29) ^ (i >> 8) ^ 0xa5) & 255 for i in range(17021))
    expected = payload + bytes((-len(payload)) % 512)
    (volume / 'ROUND.BIN').write_bytes(payload)
    (volume / 'HELLO.TXT').write_bytes(b'HC7000 SERV XM HG TRANSFER\r\n')
    u = UART(a)
    daemon = None

    def checked(command, **kwargs):
        reply = u.command(command, **kwargs)
        assert b'?' not in reply.replace(b'?BINCOM-I-No differences found', b''), reply
        return reply

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        u.open()
        u.read(.3)
        checked('SET SL OFF')
        config = checked('SHOW CONFIGURATION')
        assert f'Booted from DM{a.unit}:RT11XM'.encode() in config, config
        assert b'2048KB of memory' in config and b'22 bit addressing is on' in config
        assert b'0 Files, 0 Blocks' in checked('DIR SY:SGTEST.BIN'), 'Scratch file already exists'
        with (a.out / 'hgfsd.log').open('w') as log:
            daemon = subprocess.Popen([str(a.hgfsd), '--directory', str(volume),
                                       '--jtag-enable-adbus7'], stdout=log, stderr=subprocess.STDOUT)
            u.read(1)
            assert daemon.poll() is None, 'HG host exited; inspect hgfsd.log'
            checked('LOAD HG')
            response = checked('RUN HGTIME', timeout=180)
            assert b'RT-11 SYSTEM DATE AND TIME SET FROM HOST' in response, response
            before = time.time()
            clock = checked('TIME')
            date = checked('DATE')
            after = time.time()
            match = re.search(rb'\r\n(\d\d):(\d\d):(\d\d)', clock)
            assert match, clock
            seconds = sum(int(v) * n for v, n in zip(match.groups(), (3600, 60, 1)))
            host = time.localtime((before + after) / 2)
            host_seconds = host.tm_hour * 3600 + host.tm_min * 60 + host.tm_sec
            delta = (seconds - host_seconds + 43200) % 86400 - 43200
            assert abs(delta) < 6, (delta, clock)
            assert f'{host.tm_mday}-{time.strftime("%b-%Y", host)}'.encode() in date, date
            u.record['host_time_delta_seconds'] = delta
            assert b'HC7000 SERV XM HG TRANSFER' in checked('TYPE HG:HELLO.TXT')
            checked('COPY HG:ROUND.BIN SY:SGTEST.BIN', timeout=600)
            checked('COPY SY:SGTEST.BIN HG:RETURN.BIN', timeout=600)
            returned = volume / 'RETURN.BIN'
            deadline = time.monotonic() + 15
            while not returned.exists() or returned.stat().st_size != len(expected):
                if time.monotonic() > deadline:
                    raise TimeoutError('HG export not completed')
                u.read(.2)
            assert returned.read_bytes() == expected, 'HG/SD round-trip mismatch'
            u.record['roundtrip'] = dict(payload_bytes=len(payload), rt11_bytes=len(expected),
                sha256=hashlib.sha256(expected).hexdigest(), unit=a.unit, monitor='RT11XM')
            checked('DELETE/NOQUERY SY:SGTEST.BIN')
            assert b'0 Files, 0 Blocks' in checked('DIR SY:SGTEST.BIN')
            checked('UNLOAD HG')
            daemon.send_signal(signal.SIGTERM)
            daemon.wait(timeout=10)
            assert daemon.returncode == 0
            daemon = None
        subprocess.run([str(a.hgfsd), '--jtag-only'], check=True)
        checked('SHOW MEMORY')
        u.record['passed'] = True
    finally:
        try:
            if daemon is not None:
                daemon.send_signal(signal.SIGTERM)
                daemon.wait(timeout=10)
                subprocess.run([str(a.hgfsd), '--jtag-only'], check=True)
        finally:
            u.close()
    print('PASS SERV storage: XM HG/time and 34-sector binary round trip', flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hgfsd', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--unit', type=int, choices=range(8), required=True)
    p.add_argument('--pause-pid', type=int)
    p.add_argument('--port', default='/dev/ttyUSB1')
    a = p.parse_args()
    a.out = a.out.resolve()
    a.phase = 'serv-storage-hg'
    run(a)
