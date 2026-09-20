#!/usr/bin/env python3
"""Run the original BASIC builds and CP81 programs in SIMH as a reference."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from rt11_build import Console, ROOT


def run(out, built, errors=False):
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'test.dsk'
    if disk.exists():
        raise ValueError('Use a fresh output directory')
    shutil.copyfile(built / 'build.dsk', disk)
    rt = ROOT / '../lsi11/rt11tool'
    programs = ROOT / 'demos/rt11/basic'
    for src in sorted(programs.glob('*.BAS')):
        p = out / src.name
        p.write_bytes(src.read_text().replace('\n', '\r\n').encode('ascii'))
        subprocess.run([str(rt), 'add', str(disk), str(p), p.name], check=True, capture_output=True)
    results = []; recovery = []
    for name, cpu, options in [('B81FIS', '11/03', 'set cpu eis\nset cpu fis\n'),
                               ('B81FPU', '11/73', ''), ('B81FPD', '11/73', '')]:
        ini = out / (name + '.ini')
        # Selecting 11/03 already selects its 56 KB maximum in SIMH 3.12.
        # "set cpu 56k" is not a supported size command in that version.
        memory = '' if cpu == '11/03' else 'set cpu 64k\n'
        ini.write_text(f'set cpu {cpu}\n{memory}{options}show cpu\nset clk 50hz\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
        with (out / (name + '.log')).open('wb') as log:
            c = Console(ini, log)
            try:
                startup = c.expect(rb'RT-11FB')
                assert b'Non-existent parameter' not in startup, startup
                if cpu == '11/03':
                    assert b'11/03, EIS, FIS' in startup and b'56KB' in startup, startup
                for _ in range(3):
                    c.expect(rb'\r\n\.')
                c.send('SET SL OFF\r'); c.expect(rb'\r\n\.')
                c.send('RUN ' + name + '\r'); c.expect(rb'INDIVIDUAL\)\?')
                c.send('A\r'); c.expect(rb'READY\r\n')
                for program, marker in [('B81TST', b'CP81 PASS')] + (
                        [('B81DBL', b'CP81 DOUBLE PASS')] if name == 'B81FPD' else []):
                    c.send('RUN ' + program + '\r')
                    response = c.expect(rb'READY\r\n', timeout=120)
                    assert marker in response and b'FAIL' not in response and b'BAD' not in response, response
                    if program == 'B81TST':
                        assert b'CHECKS= 29  ERRORS= 0' in response, response
                    results.append(dict(basic=name, cpu=cpu, program=program,
                                        response=response.decode('ascii'), pass_=True))
                if errors:
                    for command, expected in (
                        ('PRINT 1/0', b'?DIVISION BY ZERO'),
                        ('PRINT SQR(-1)', b'?NEGATIVE SQUARE ROOT'),
                        ('PRINT 1E30*1E30', b'?FLOATING OVERFLOW'),
                        ('PRINT 2+2', b'\r\n 4 \r\n')):
                        c.send(command + '\r'); response = c.expect(rb'READY\r\n')
                        assert expected in response, response
                        recovery.append(dict(basic=name, command=command, response=response.decode('ascii')))
                c.send('BYE\r'); c.expect(rb'\r\n\.')
            finally:
                c.close()
    record = dict(results=results, recovery=recovery, inputs={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(programs.glob('*.BAS'))})
    (out / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--built', type=Path, required=True)
    ap.add_argument('--errors', action='store_true', help='also test floating errors and recovery')
    a = ap.parse_args()
    print(json.dumps(run(a.out.resolve(), a.built.resolve(), a.errors), indent=2))
