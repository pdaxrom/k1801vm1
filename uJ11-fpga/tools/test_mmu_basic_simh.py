#!/usr/bin/env python3
"""Check BASIC on SIMH 11/73 using a private FB disk converted to XM.

The production SD and the input image are never written. All phases require
their own command echo and completion; a HALT or unexpected monitor exit fails.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from rt11_build import Console, ROOT


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(out, base):
    out.mkdir(parents=True, exist_ok=False)
    disk = out / 'fb-to-xm.dsk'
    rt = ROOT / '../lsi11/rt11tool'
    simh = Path(shutil.which('pdp11')).resolve()
    record = dict(passed=False, cpu='11/73', memory_kib=2048, clock_hz=50,
                  base=str(base), base_sha256=sha(base), simulator=str(simh),
                  simulator_sha256=sha(simh), commands=[], phases={})
    record['inputs'] = {str(p.relative_to(ROOT)): sha(p) for p in (
        Path(__file__).resolve(), ROOT / 'tools/rt11_build.py',
        ROOT / 'releases/basic/B81FPU.SAV', ROOT / 'releases/basic/B81FPD.SAV',
        ROOT / 'demos/rt11/basic/B81TST.BAS', ROOT / 'demos/rt11/basic/B81DBL.BAS')}

    def save():
        (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')

    def extract(directory, names):
        directory.mkdir()
        for name in names:
            subprocess.run([str(rt), 'extract', str(disk), str(directory), name],
                           check=True, capture_output=True)
        return {name: sha(directory / name) for name in names}

    def command(c, phase, text, pattern=rb'\r\n\.', expected=(), timeout=30):
        step = dict(phase=phase, command=text, complete=False)
        record['commands'].append(step)
        save()
        c.send(text + '\r')
        response = c.expect(pattern, timeout)
        step['response'] = response.decode('ascii', 'backslashreplace')
        assert text.encode() in response, response
        assert all(item in response for item in expected), response
        step['complete'] = True
        save()
        print(step['response'], end='', flush=True)
        return response

    def boot(c, phase, monitor):
        response = c.expect(rb'RT-11' + monitor.encode() + rb'[^\r\n]*V05\.03', 20)
        # Both verified, unchanged startup files contain two SET commands.
        for _ in range(3):
            response += c.expect(rb'\r\n\.', 20)
        record['phases'][phase] = dict(fresh_banner=True,
                                      banner=response.decode('ascii', 'backslashreplace'))
        command(c, phase, 'SET SL OFF')
        expected = [b'PDP 11/73A Processor', b'2048KB of memory',
                    b'Floating Point Microcode', b'Extended Instruction Set (EIS)',
                    b'Memory Management Unit', b'50 Cycle System Clock',
                    b'Booted from DM0:RT11' + monitor.encode()]
        if monitor == 'XM':
            expected.append(b'22 bit addressing is on')
        command(c, phase, 'SHOW CONFIGURATION', expected=expected)

    def basic(c, phase, name):
        ready = rb'\r\nREADY\r\n'
        command(c, phase, 'RUN ' + name, rb'INDIVIDUAL\)\?')
        command(c, phase, 'A', ready)
        response = command(c, phase, 'RUN B81TST', ready,
                           [b'CHECKS= 29  ERRORS= 0', b'CP81 PASS'], 120)
        assert b'FAIL' not in response and b'BAD' not in response, response
        if name == 'B81FPD':
            response = command(c, phase, 'RUN B81DBL', ready,
                               [b'CP81 DOUBLE PASS'], 60)
            assert b'FAIL' not in response, response
        for text, expected in (
            ('PRINT 1/0', b'?DIVISION BY ZERO'),
            ('PRINT 2+2', b'\r\n 4 \r\n'),
            ('PRINT SQR(-1)', b'?NEGATIVE SQUARE ROOT'),
            ('PRINT 2+2', b'\r\n 4 \r\n'),
            ('PRINT 1E30*1E30', b'?FLOATING OVERFLOW'),
            ('PRINT 2+2', b'\r\n 4 \r\n'),
            ('PRINT 1/0', b'?DIVISION BY ZERO'),
            ('PRINT 2+2', b'\r\n 4 \r\n'),
        ):
            command(c, phase, text, ready, [expected])
        command(c, phase, 'BYE')

    try:
        shutil.copyfile(base, disk)
        preserved = ['RT11FB.SYS', 'RT11XM.SYS', 'DM.SYS', 'DMX.SYS',
                     'STARTF.COM', 'STARTX.COM']
        record['preserved_before'] = extract(out / 'before', preserved)
        for name in ['STARTF.COM', 'STARTX.COM']:
            assert (out / 'before' / name).read_bytes().rstrip(b'\0') == (
                b'SET TT SCOPE,NOCRLF\r\nSET SL ON\r\n')
        for name in ['B81FPU.SAV', 'B81FPD.SAV', 'B81TST.BAS', 'B81DBL.BAS']:
            if name.endswith('.BAS'):
                src = ROOT / 'demos/rt11/basic' / name
                path = out / name
                path.write_bytes(src.read_text().replace('\n', '\r\n').encode('ascii'))
            else:
                path = ROOT / 'releases/basic' / name
            subprocess.run([str(rt), 'add', str(disk), str(path), name],
                           check=True, capture_output=True)
        record['fb_disk_sha256'] = sha(disk)
        boot_before = disk.read_bytes()[:512]
        (out / 'boot-fb.bin').write_bytes(boot_before)
        ini = out / 'reference.ini'
        # Selecting the Qbus 11/73 disables HK; enable it explicitly afterward
        # to exercise the same RK611/DM system disk used by the FPGA.
        ini.write_text('set cpu 11/73\nset cpu 2m\nset clk 50hz\n'
                       'set hk enable\nset hk0 rk07\nshow cpu\nshow clk\n'
                       f'show hk\nshow version\nattach hk0 {disk}\nboot hk0\n')
        with (out / 'fb-to-xm.log').open('wb') as log:
            c = Console(ini, log)
            try:
                boot(c, 'fb', 'FB')
                for name in ['B81FPU', 'B81FPD']:
                    basic(c, 'fb', name)
                record['phases']['fb']['basic_passed'] = True
                command(c, 'convert', 'COPY/BOOT SY:RT11XM.SYS SY:')
                c.send('BOOT SY:\r')
                boot(c, 'xm-reboot', 'XM')
            finally:
                c.close()
        boot_after = disk.read_bytes()[:512]
        (out / 'boot-xm.bin').write_bytes(boot_after)
        assert boot_after != boot_before, 'BOOT block was not changed'
        # A fresh simulator must use the converted disk's own bootstrap.
        with (out / 'xm-cold-basic.log').open('wb') as log:
            c = Console(ini, log)
            try:
                boot(c, 'xm-cold', 'XM')
                command(c, 'xm-cold', 'SHOW MEMORY', expected=[b'10000000  MEMTOP'])
                for name in ['B81FPU', 'B81FPD']:
                    basic(c, 'xm-cold', name)
                record['phases']['xm-cold']['basic_passed'] = True
                command(c, 'xm-cold', 'SHOW CONFIGURATION', expected=[b'Booted from DM0:RT11XM'])
            finally:
                c.close()
        record['preserved_after'] = extract(out / 'after', preserved)
        assert record['preserved_before'] == record['preserved_after']
        record['xm_disk_sha256'] = sha(disk)
        record['passed'] = True
    finally:
        record['base_unchanged'] = sha(base) == record['base_sha256']
        save()
        assert record['base_unchanged'], 'Input image changed'
    print('\nPASS SIMH 11/73: FB -> persistent XM; BASIC single/double and exception recovery')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--base', type=Path, default=ROOT / '../lsi11-fpga/images/rt11v503.dsk')
    a = p.parse_args()
    run(a.out.resolve(), a.base.resolve())
