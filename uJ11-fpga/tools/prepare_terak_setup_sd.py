#!/usr/bin/env python3
"""Upgrade TBIOS.BIN and preallocate TERAK.SET in a backed-up shared card.

Only partition 1 FAT16 changes. This tool never writes a physical device.
Existing settings, other files and all disk partitions are preserved.
"""
import argparse
import io
import json
import stat
import struct
from pathlib import Path

from prepare_terak_bios_sd import Fat16, digest
from terak_mbr import partitions


def prepare(source, bios, out, capacity):
    if not stat.S_ISREG(source.stat().st_mode):
        raise ValueError('Use a regular backup image, never a physical device')
    before = source.read_bytes()
    table = partitions(io.BytesIO(before))
    if len(before) % 512 or len(before)//512 > capacity or any(
            (p['start']+p['blocks'])*512 > len(before) for n, p in table.items() if p and n != 4):
        raise ValueError('Backup must contain all existing disks and fit the card')
    fat = table.get(1)
    if not fat or fat['type'] not in (6, 14):
        raise ValueError('Partition 1 must be FAT16')
    boot = bios.read_bytes()
    if len(boot) < 18 or boot[:4] != b'UJTB':
        raise ValueError('Expected a generated TBIOS.BIN')
    size, checksum, version = struct.unpack_from('<III', boot, 4)
    if version != 1 or not size or size > 4096 or size % 2 or size+16 != len(boot) or \
            sum(struct.unpack('<'+'H'*(size//2), boot[16:])) & 65535 != checksum or \
            b'DEL: Enter BIOS Setup' not in boot:
        raise ValueError('Invalid Setup BIOS length/version/checksum')
    lo, hi = fat['start']*512, (fat['start']+fat['blocks'])*512
    volume = Fat16(before[lo:hi])
    old = volume.files()
    if any(name not in old for name in ('TBIOS.BIN', 'TERAK.CFG')):
        raise ValueError('Expected an existing TERAK BIOS card')
    if 'TERAK.SET' in old:
        offset, entry, data = old['TERAK.SET']
        chain = volume.chain(struct.unpack_from('<H', entry, 26)[0])
        if len(data) != 1024 or entry[11] & 1 or len(chain) != (1024+volume.cluster_size-1)//volume.cluster_size:
            raise ValueError('Existing TERAK.SET must be writable and exactly 1024 bytes')
    else:
        volume.put('TERAK.SET', bytes(1024))
    if 'TBIOS.BAK' not in old and old['TBIOS.BIN'][2] != boot:
        volume.put('TBIOS.BAK', old['TBIOS.BIN'][2])
    volume.put('TBIOS.BIN', boot)
    new = volume.files()
    preserved = {name: value for name, value in old.items() if name != 'TBIOS.BIN'}
    if any(new[name] != value for name, value in preserved.items()):
        raise AssertionError('Existing FAT file or directory metadata changed')
    _, entry, settings = new['TERAK.SET']
    chain = volume.chain(struct.unpack_from('<H', entry, 26)[0])
    positions = [fat['start']+(volume.first+(c-2)*volume.cluster_size)//512+b
                 for c in chain for b in range(volume.spc)][:2]
    if len(positions) != 2 or positions[0] == positions[1]:
        raise AssertionError('Settings records need two separate sectors')
    after = bytearray(before)
    after[lo:hi] = volume.data
    if partitions(io.BytesIO(after)) != table or after[:lo] != before[:lo] or after[hi:] != before[hi:]:
        raise AssertionError('Partition table or another disk changed')
    regions = [dict(name='fat16.bin', start_lba=fat['start'], sectors=fat['blocks'],
                    sha256=digest(volume.data), before_sha256=digest(before[lo:hi]))]
    record = dict(source=str(source.resolve()), source_bytes=len(before), source_sha256=digest(before),
                  card_sectors=capacity, candidate_sha256=digest(after), bios_sha256=digest(boot),
                  preserved_files={name: digest(v[2]) for name, v in preserved.items()},
                  replaced_files={'TBIOS.BIN': digest(old['TBIOS.BIN'][2])},
                  partitions=table, settings_lbas=positions, regions=regions)
    out.mkdir(parents=True, exist_ok=False)
    (out/'card-after.img').write_bytes(after)
    (out/'fat16.bin').write_bytes(volume.data)
    (out/'plan.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--bios', type=Path, required=True)
    parser.add_argument('--card-sectors', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.source, args.bios, args.out, args.card_sectors)
    print(json.dumps(dict(settings_lbas=result['settings_lbas'], preserved_files=len(result['preserved_files']),
                         candidate_sha256=result['candidate_sha256']), indent=2))
