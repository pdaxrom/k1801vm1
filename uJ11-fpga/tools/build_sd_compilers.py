#!/usr/bin/env python3
"""Prepare the five RT-11 compiler disks for an existing HC7000 SD layout.

RK0 remains available for RT-11 V4. Only private output files are written;
this tool never accesses a physical card or modifies the distribution.
"""
import argparse
import hashlib
import json
from pathlib import Path

from board_common import ROOT
from build_sd_image import directory

MAPPING = (
    ('system', 'rk07', 0, 'DM0', 'SY'),
    ('storage', 'rk07', 1, 'DM1', 'VOL'),
    ('basic', 'rk05', 1, 'RK1', 'BAS'),
    ('pascal', 'rk05', 2, 'RK2', 'PAS'),
    ('fortran', 'rk05', 3, 'RK3', 'FOR'),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(out):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT.parent / 'lsi11/disks/rt11v5.3'
    originals = {name: (source / (name + '.dsk')).read_bytes() for name, *_ in MAPPING}
    system = bytearray(originals['system'])
    before, _, _ = directory(system)
    startup = {}
    for name in ('STARTX.COM', 'STARTF.COM', 'STARTS.COM'):
        entry = before[name]
        start = entry['start_block'] * 512
        end = start + entry['bytes']
        payload = bytes(system[start:end])
        original = payload
        assert payload.count(b'ASSIGN DM1: VOL:') == 1, name
        for old, new in ((2, 3), (1, 2), (0, 1)):
            # Descending order prevents RK0 -> RK1 -> RK2 -> RK3 cascades.
            alias = ('BAS', 'PAS', 'FOR')[old]
            a = f'ASSIGN RK{old}: {alias}:'.encode('ascii')
            b = f'ASSIGN RK{new}: {alias}:'.encode('ascii')
            assert payload.count(a) == 1, (name, a)
            payload = payload.replace(a, b)
        assert len(payload) == len(original)
        assert sum(a != b for a, b in zip(payload, original)) == 3
        system[start:end] = payload
        (out / name).write_bytes(payload.rstrip(b'\0'))
        startup[name] = dict(start_block=entry['start_block'], blocks=entry['blocks'],
                             before_sha256=sha(original), after_sha256=sha(payload))
    after, _, _ = directory(system)
    assert system[:3072] == originals['system'][:3072]
    assert sum(a != b for a, b in zip(system, originals['system'])) == 9
    for name, info in before.items():
        assert after[name]['start_block'] == info['start_block']
        assert after[name]['blocks'] == info['blocks']
        if name not in startup:
            assert after[name]['sha256'] == info['sha256'], name
    outputs = []
    for name, media, unit, rt_device, logical in MAPPING:
        data = bytes(system) if name == 'system' else originals[name]
        blocks = 53790 if media == 'rk07' else 4872
        assert len(data) % 512 == 0 and len(data) <= blocks * 512
        data = data.ljust(blocks * 512, b'\0')
        path = out / (name + '.dsk')
        path.write_bytes(data)
        if name != 'system':
            assert data[:len(originals[name])] == originals[name]
            assert not any(data[len(originals[name]):])
        outputs.append(dict(name=name, file=path.name, media=media, unit=unit,
                            rt_device=rt_device, logical=logical, blocks=blocks,
                            bytes=len(data), sha256=sha(data),
                            source=str((source / path.name).relative_to(ROOT.parent)),
                            source_bytes=len(originals[name]), source_sha256=sha(originals[name])))
    for name, data in originals.items():
        assert sha((source / (name + '.dsk')).read_bytes()) == sha(data)
    result = dict(passed=True, scope='Private media preparation; not a physical SD write',
                  boot_controller='rh', boot_unit=0, startup=startup, disks=outputs,
                  system_changed_bytes=9, source_disks_unchanged=True,
                  rk05_padding='Zero extension to the fixed 4872-sector geometry',
                  builder_sha256=sha(Path(__file__).read_bytes()))
    (out / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    (out / 'SHA256SUMS').write_text(''.join(f'{d["sha256"]}  {d["file"]}\n' for d in outputs))
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    build(p.parse_args().out)
