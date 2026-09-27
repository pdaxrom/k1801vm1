#!/usr/bin/env python3
"""Capture one physical RESET and test the HC7000 SD boot menu over UART.

The operator presses long RESET only after ARMED. Requires the qualification
card containing RH unit 7 alone; never reprograms the FPGA or changes SD data.
"""
import argparse
import os
import re
import signal
import time
from pathlib import Path
from hardware_modules import UART


def run(a):
    u = UART(a)

    def expect(text, offset, timeout):
        deadline = time.monotonic() + timeout
        while text not in u.capture[offset:]:
            if time.monotonic() > deadline:
                raise TimeoutError('Missing UART text: ' + repr(text))
            u.read(.02)
        return time.monotonic()

    def key(value):
        offset = len(u.capture)
        assert os.write(u.fd, value) == len(value)
        return offset

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        u.open()
        u.read(.3)
        offset = len(u.capture)
        u.record['case'] = a.case
        u.record['boot_offset'] = offset
        u.save()
        print('[ARMED] Long RESET required now; do not type into the UART.', flush=True)
        expect(b'uJ11 SD BOOT MENU', offset, a.reset_timeout)
        started = expect(b'other key=menu: ', offset, 30)
        assert b'2 - RH11/HK, units: 7 \r\n' in u.capture[offset:]
        assert b'Default: RH7 - boot in 5s' in u.capture[offset:]
        if a.case == 'enter':
            key(b'\r')
        elif a.case == 'cancel':
            mark = key(b'm')
            expect(b'Select controller [1/2/3/4/5/6]: ', mark, 5)
            u.read(6)
            assert b'Booting ' not in u.capture[offset:], 'Cancellation did not stop autoboot'
            u.record['cancel_hold_seconds'] = 6
            mark = key(b'1')
            expect(b'Controller not present in this system.', mark, 5)
            mark = key(b'2')
            expect(b'Select unit [0..7]: ', mark, 5)
            mark = key(b'0')
            expect(b'Unit not present.', mark, 5)
            key(b'7')
        booting = expect(b'Booting RH/HK bootstrap...', offset, 10)
        elapsed = booting - started
        u.record['menu_to_boot_seconds'] = elapsed
        if a.case == 'auto':
            assert 4.8 <= elapsed <= 5.5, elapsed
        elif a.case == 'enter':
            assert elapsed < 1, elapsed
        expect(b'RT-11XM (S) V05.03', offset, 120)
        expect(b'HC7000 XM KIT READY', offset, 90)
        marker = bytes(u.capture).index(b'HC7000 XM KIT READY', offset)
        deadline = time.monotonic() + 30
        while not re.search(rb'\r\n\.', u.capture[marker:]):
            if time.monotonic() > deadline:
                raise TimeoutError('No final startup prompt')
            u.read(.1)
        u.read(.3)
        response = u.command('SET SL OFF')
        assert b'?' not in response, response
        config = u.command('SHOW CONFIGURATION')
        assert b'Booted from DM7:RT11XM' in config, config
        assert b'22 bit addressing is on' in config and b'2048KB of memory' in config
        u.record['passed'] = True
    finally:
        u.close()
    print('PASS physical SD menu: ' + a.case, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', choices=('auto', 'enter', 'cancel'), required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--reset-timeout', type=float, default=900)
    p.add_argument('--pause-pid', type=int)
    p.add_argument('--port', default='/dev/ttyUSB1')
    a = p.parse_args()
    a.out = a.out.resolve()
    a.phase = 'serv-storage-menu'
    run(a)
