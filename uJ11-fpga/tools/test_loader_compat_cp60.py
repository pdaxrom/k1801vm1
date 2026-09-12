#!/usr/bin/env python3
"""Check that UJLOAD exits cleanly on a DCJ11 without service instructions."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from rt11_build import Console


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asm-dir', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    asm = args.asm_dir.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    base = asm/'build.dsk'
    digest = sha(base)
    manifest = json.loads((asm/'build-inputs.json').read_text())
    assert sha(asm/'UJLOAD.SAV') == manifest['outputs']['UJLOAD.SAV']
    disk = out/'test.dsk'
    shutil.copyfile(base, disk)
    ini = out/'run.ini'
    ini.write_text(f'set cpu 11/73\nset cpu 64k\nset clk 50hz\nset hk0 rk07\nattach hk0 {disk}\nboot hk0\n')
    with (out/'uart.log').open('wb') as log:
        console = Console(ini, log)
        try:
            console.expect(rb'RT-11FB')
            for _ in range(3):
                console.expect(rb'\r\n\.')
            console.send('SET SL OFF\r')
            console.expect(rb'\r\n\.')
            console.send('RUN UJLOAD\r')
            console.expect(rb'\?UJLOAD-E-Service instructions unavailable')
            console.expect(rb'\r\n\.')
            console.send('DIR UJLOAD.SAV\r')
            listing = console.expect(rb'\r\n\.')
            assert b'UJLOAD' in listing and b'Files' in listing, listing
        finally:
            console.close()
    assert sha(base) == digest, 'modified input build disk'
    result = dict(passed=True, cpu='SIMH 11/73, 64K, RT-11FB V05.03',
                  unsupported_message=True, subsequent_directory=True,
                  sav_sha256=sha(asm/'UJLOAD.SAV'), input_disk_sha256=digest,
                  uart_sha256=sha(out/'uart.log'), driver_sha256=sha(Path(__file__)))
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
