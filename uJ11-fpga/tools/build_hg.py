#!/usr/bin/env python3
"""Build HG.SYS, HGTIME.SAV and CLOCK.SAV/.REL with native RT-11 MACRO/LINK."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from board_common import ROOT
from rt11_build import Console

SOURCES = ['demos/rt11/hostdisk/HG.MAC', 'demos/rt11/hostdisk/HGTIME.MAC',
           'demos/rt11/panel/TMDRV.MAC', 'demos/rt11/panel/CLOCK.MAC',
           'demos/rt11/panel/PNLDRV.MAC']


def build(out, base=ROOT/'../lsi11-fpga/images/rt11v503.dsk', overrides=None):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out/'build.dsk'
    if disk.exists():
        raise ValueError('Use a fresh build directory: '+str(out))
    base_hash = hashlib.sha256(base.read_bytes()).hexdigest()
    shutil.copyfile(base, disk)
    rt = ROOT/'../lsi11/rt11tool'
    for name in SOURCES:
        src = ROOT/name
        target = out/src.name
        text = (overrides or {}).get(name, src.read_text())
        target.write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('ascii'))
        subprocess.run([str(rt), 'add', str(disk), str(target), src.name], check=True, capture_output=True)
    ini = out/'build.ini'
    ini.write_text(f'set cpu 11/73\nset cpu 64k\nset clk 50hz\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
    with (out/'console.log').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'RT-11FB')
            for _ in range(3):
                c.expect(rb'\r\n\.')
            c.send('SET SL OFF\r'); c.expect(rb'\r\n\.')
            for source in SOURCES:
                name = Path(source).stem
                c.send('R MACRO\r'); c.expect(rb'\*')
                c.send(f'{name},{name}={name}\r'); text = c.expect(rb'\*')
                if b'Errors detected' in text or b'?MACRO' in text:
                    raise RuntimeError(text.decode(errors='replace'))
                c.send('\x03'); c.expect(rb'\r\n\.')
            for command in ('LINK/NOBITMAP/EXECUTE:HG.SYS HG',
                            'LINK/MAP:HGTIME HGTIME,TMDRV',
                            'LINK/MAP:CLOCK CLOCK,TMDRV,PNLDRV',
                            'LINK/FOREGROUND/MAP:CLKFG CLOCK,TMDRV,PNLDRV'):
                c.send(command+'\r'); text = c.expect(rb'\r\n\.')
                if b'?' in text:
                    raise RuntimeError(text.decode(errors='replace'))
        finally:
            c.close()
    files = ['HG.SYS', 'HGTIME.SAV', 'CLOCK.SAV', 'CLOCK.REL',
             'HGTIME.MAP', 'CLOCK.MAP', 'CLKFG.MAP']
    files += [Path(p).stem+ext for p in SOURCES for ext in ('.LST', '.OBJ')]
    for name in files:
        subprocess.run([str(rt), 'extract', str(disk), str(out), name], check=True, capture_output=True)
        if name.endswith('.LST'):
            assert re.search(r'Errors detected:\s+0\b', (out/name).read_text(errors='replace')), name
    assert hashlib.sha256(base.read_bytes()).hexdigest() == base_hash
    record = dict(tool='RT-11 V5.03 MACRO/LINK in SIMH', base_sha256=base_hash,
                  sources={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in SOURCES},
                  outputs={p: hashlib.sha256((out/p).read_bytes()).hexdigest() for p in files})
    if overrides:
        record['test_overrides'] = {p: hashlib.sha256(text.encode()).hexdigest()
                                    for p, text in overrides.items()}
    (out/'build-inputs.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build(args.out)
    print('Built HG.SYS, HGTIME.SAV, CLOCK.SAV and CLOCK.REL in', args.out)
