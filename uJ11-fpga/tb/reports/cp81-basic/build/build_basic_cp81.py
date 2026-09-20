#!/usr/bin/env python3
"""Configure DEC BASIC-11 V2 for FIS/FPU using its original SUCNFG and LINK.

The source images are read-only; every write targets a fresh private disk.
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


def build(out, kit_image, base):
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'build.dsk'
    if disk.exists():
        raise ValueError('Use a fresh build directory')
    hashes = {str(p): sha(p) for p in (kit_image, base)}
    rt = ROOT / '../lsi11/rt11tool'
    kit = out / 'kit'
    kit.mkdir()
    subprocess.run([str(rt), 'extract', str(kit_image), str(kit)], check=True, capture_output=True)
    shutil.copyfile(base, disk)
    selected = sorted(kit.iterdir())
    # Native COPY grows RT-11 directory segments; rt11tool add does not.
    # Present a private, padded copy of the distribution on the second RK07.
    kit_disk = out / 'kit.dsk'
    shutil.copyfile(kit_image, kit_disk)
    with kit_disk.open('r+b') as f:
        f.truncate(53790 * 512)
    ini = out / 'build.ini'
    ini.write_text(f'set cpu 11/73\nset cpu 64k\nset clk 50hz\nset hk0 rk07\nset hk1 rk07\nattach hk0 {disk}\nattach -r hk1 {kit_disk}\nboot hk0\n')
    variants = [('B81FIS', 'S', 'FIS'), ('B81FPU', 'S', 'FPU'), ('B81FPD', 'D', 'FPU')]
    with (out / 'console.log').open('wb') as log:
        c = Console(ini, log)
        try:
            c.expect(rb'RT-11FB')
            for _ in range(3):
                c.expect(rb'\r\n\.')
            c.send('SET SL OFF\r'); c.expect(rb'\r\n\.')
            c.send('COPY DK1:*.* DK0:\r')
            response = c.expect(rb'\r\n\.', timeout=120)
            assert b'?PIP' not in response and b'?DUP' not in response, response
            for name, precision, hardware in variants:
                print('Configuring', name, precision, hardware, flush=True)
                c.send('R BASIC\r'); c.expect(rb'INDIVIDUAL\)\?')
                c.send('A\r'); c.expect(rb'READY\r\n')
                c.send('RUN SUCNFG\r')
                answers = [name, 'Y', '', 'B', 'Y', 'N', precision, hardware,
                           'Y', 'Y', 'Y', 'Y', '2']
                for answer in answers:
                    c.expect(rb'\): ')
                    c.send(answer + '\r')
                c.expect(rb'READY\r\n')
                c.send('BYE\r'); c.expect(rb'\r\n\.')
                c.send('@' + name + '\r')
                # An indirect file echoes '.R LINK'; it is not completion.
                # No CALL support intentionally leaves exactly these symbols.
                response = c.expect(rb'\?LINK-W-Undefined globals:\r\n', timeout=120)
                assert response.count(b'?LINK') == 1, response
                response = c.expect(rb'\.\.MSP\$\r\n', timeout=120)
                undefined = re.findall(rb'\.\.[A-Z]+\$', response)
                assert set(undefined) == {b'..UAC$', b'..NRC$', b'..MSP$'}, response
                assert b'?LINK' not in response, response
                c.expect(rb'\r\n\.')
                c.send('DIR ' + name + '.SAV\r'); result = c.expect(rb'\r\n\.')
                assert b'No files' not in result and name.encode() in result, result
        finally:
            c.close()
    outputs = {}
    for name, precision, hardware in variants:
        for ext in ('SAV', 'COM', 'MAP'):
            p = out / (name + '.' + ext)
            subprocess.run([str(rt), 'extract', str(disk), str(out), p.name], check=True, capture_output=True)
            outputs[p.name] = sha(p)
        com = (out / (name + '.COM')).read_text(errors='replace')
        assert 'BSOT0' + precision + '.' + hardware in com, com
        mapping = (out / (name + '.MAP')).read_text(errors='replace')
        section = re.search(r'Undefined globals:\s*(.*?)\s*Transfer address', mapping, re.S)
        assert section, 'Missing final LINK map section: ' + name
        undefined = section[1].split()
        assert sorted(undefined) == ['..MSP$', '..NRC$', '..UAC$'], undefined
        assert '/O:1' in com and '/O:2' in com, 'Expected overlay type 2'
    assert all(sha(Path(p)) == h for p, h in hashes.items()), 'Source image changed'
    record = dict(inputs=hashes, kit_files={p.name: sha(p) for p in selected},
                  variants=variants, overlay_type=2, outputs=outputs,
                  builder_sha256=sha(Path(__file__)))
    (out / 'build-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--kit', type=Path, default=ROOT / '../lsi11/disks/rt11v5.3/basic.dsk')
    ap.add_argument('--base', type=Path, default=ROOT / '../lsi11-fpga/images/rt11v503.dsk')
    a = ap.parse_args()
    print(json.dumps(build(a.out.resolve(), a.kit.resolve(), a.base.resolve()), indent=2))
