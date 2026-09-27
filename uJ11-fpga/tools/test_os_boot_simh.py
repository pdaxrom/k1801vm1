#!/usr/bin/env python3
"""Reference boot matrix using private disks; this does not qualify FPGA RTL."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import time

from board_common import ROOT
from rt11_build import Console


CASES = {
    'rt11xm': [('hk0', 'rt11v5.3/system.dsk'),
               ('hk1', 'rt11v5.3/storage.dsk'),
               ('rk0', 'rt11v5.3/basic.dsk'),
               ('rk1', 'rt11v5.3/pascal.dsk'),
               ('rk2', 'rt11v5.3/fortran.dsk')],
    'rt11v4': [('rk0', 'rt11v400.dsk')],
    'rsx': [('rq0', 'rsx11/rsxm11sys.dsk'),
            ('rq1', 'rsx11/rsx11mpbl87.dsk')],
}


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def quiet(console, seconds=0.5, timeout=15):
    """Collect the remaining startup commands after the first monitor prompt."""
    deadline = time.monotonic() + timeout
    last = time.monotonic()
    while time.monotonic() < deadline:
        if select.select([console.fd], [], [], 0.1)[0]:
            data = os.read(console.fd, 65536)
            if not data:
                raise RuntimeError('SIMH console closed')
            console.log.write(data)
            console.log.flush()
            console.buffer += data
            last = time.monotonic()
        elif time.monotonic() - last >= seconds:
            result, console.buffer = console.buffer, b''
            return result
    raise TimeoutError('Console did not settle')


def run(case, out, cpu='11/70', rsx_boot_unit=0):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT.parent / 'lsi11/disks'
    inputs = {name: sha(source / name) for _, name in CASES[case]}
    boot = f'rq{rsx_boot_unit}' if case == 'rsx' else CASES[case][0][0]
    simulator = Path(shutil.which('pdp11')).resolve()
    record = dict(passed=False, scope='SIMH reference, not FPGA qualification',
                  case=case, cpu=cpu, memory_bytes=2097152, clock_hz=50,
                  fpp='disabled' if cpu == '11/70' else 'SIMH fixed CPU capability',
                  boot=boot, inputs=inputs, commands=[],
                  simulator=str(simulator), simulator_sha256=sha(simulator),
                  tools={str(p.relative_to(ROOT)): sha(p) for p in
                         (Path(__file__).resolve(), ROOT / 'tools/rt11_build.py')})
    lines = [f'set cpu {cpu}', 'set cpu 2m', 'set clk 50hz',
             'set tti 7b', 'set tto 7b']
    # SIMH 3.12 rejects NOFPP for 11/73, whose CPU definition fixes that feature.
    if cpu == '11/70':
        lines.append('set cpu nofpp')
    for controller in ('hk', 'rk', 'rq'):
        lines.append(f'set {controller} ' +
                     ('enable' if any(u.startswith(controller) for u, _ in CASES[case]) else 'disable'))
    for unit, name in CASES[case]:
        disk = out / Path(name).name
        shutil.copyfile(source / name, disk)
        if unit.startswith('hk'):
            lines.append(f'set {unit} rk07')
        if unit.startswith('rq'):
            lines.append(f'set {unit} rd54')
        lines.append(f'attach {unit} {disk}')
    lines += ['show version', 'show cpu', f'boot {boot}']
    ini = out / 'test.ini'
    ini.write_text('\n'.join(lines) + '\n')
    console = None
    simulator_prompt = False

    def command(text, expected=(), prompt=rb'\r\n\.'):
        console.send(text + '\r')
        console.expect(re.escape(text.encode()) + rb'\r+\n', 60)
        response = console.expect(prompt, 90)
        record['commands'].append(dict(command=text, response=response.decode('ascii', 'backslashreplace')))
        assert all(token in response for token in expected), (text, response)
        assert not re.search(rb'\?(?:KMON|DIR|PIP|DUP)|HALT instruction|Invalid argument', response), response
        return response

    try:
        with (out / 'uart.txt').open('wb') as log:
            console = Console(ini, log)
            try:
                if case == 'rsx' and rsx_boot_unit == 0:
                    response = console.expect(rb'sim> ', 90)
                    simulator_prompt = True
                    assert b'THIS VOLUME DOES NOT CONTAIN A HARDWARE BOOTABLE SYSTEM' in response, response
                    assert b'HALT instruction, PC: 000034' in response, response
                    record.update(status='not_bootable', failure='RQ0 volume has a nonbootable placeholder boot block',
                                  boot_output=response.decode('ascii', 'backslashreplace'))
                elif case == 'rsx':
                    response = console.expect(rb'Please enter time and date[^\n]*\[S\]: ', 90)
                    assert b'RSX-11M-PLUS V4.6  BL87' in response, response
                    assert b'Invalid argument' not in response, response
                    record['boot_output'] = response.decode('ascii', 'backslashreplace')
                    console.send('12:00 28-SEP-1999\r')
                    response = console.expect(rb'(?:\r\n|\n\r)>', 60) + quiet(console, seconds=2)
                    record['startup_output'] = response.decode('ascii', 'backslashreplace')
                    command('DEV DU:', [b'DU0:', b'DU1:'], rb'(?:\r\n|\n\r)>')
                    command('PIP DU1:[1,54]RSX11M.SYS/LI',
                            [b'RSX11M.SYS;1', b'Total of 1026./1026. blocks in 1. file'],
                            rb'(?:\r\n|\n\r)>')
                    record.update(passed=True, status='booted')
                else:
                    banner = rb'RT-11XM[^\r\n]*V05\.03' if case == 'rt11xm' else rb'RT-11SJ[^\r\n]*V04\.00C'
                    response = console.expect(banner, 60)
                    assert b'Invalid argument' not in response, response
                    response += console.expect(rb'\r\n\.', 60) + quiet(console)
                    record['boot_output'] = response.decode('ascii', 'backslashreplace')
                    if case == 'rt11xm':
                        command('SET SL OFF')
                        command('SHOW CONFIGURATION', [b'Booted from DM0:RT11XM', b'2048KB of memory', b'22 bit addressing is on'])
                        command('SHOW MEMORY', [b'10000000  MEMTOP'])
                        for text, expected in [('DIR DM0:RT11*.SYS', b'RT11XM'),
                                               ('DIR DM1:', b'ADDER'),
                                               ('DIR RK0:BASIC.SAV', b'BASIC'),
                                               ('DIR RK1:XM.SAV', b'XM'),
                                               ('DIR RK2:FORTRA.SAV', b'FORTRA')]:
                            command(text, [expected])
                    else:
                        command('DIR RK0:RT11*.SYS', [b'RT11SJ'])
                        command('TYPE V4USER.TXT', [b'Welcome to RT-11 Version 4'])
                    record.update(passed=True, status='booted')
            finally:
                if simulator_prompt:
                    try:
                        time.sleep(0.1)
                        console.send('quit\n')
                        console.expect(rb'Goodbye', 5)
                        console.process.wait(timeout=5)
                    finally:
                        if console.process.poll() is None:
                            console.process.kill()
                            console.process.wait(timeout=5)
                        os.close(console.fd)
                else:
                    console.close()
    except Exception as error:
        record.update(status='failed', error=repr(error))
        raise
    finally:
        record['originals_unchanged'] = all(sha(source / name) == digest for name, digest in inputs.items())
        (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        assert record['originals_unchanged'], 'Source disk changed'
    print(f"{case} {boot}: {record['status']} ({cpu})", flush=True)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=CASES, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cpu', choices=('11/70', '11/73'), default='11/70')
    parser.add_argument('--rsx-boot-unit', type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    result = run(args.case, args.out, args.cpu, args.rsx_boot_unit)
    raise SystemExit(0 if result['passed'] else 2)
