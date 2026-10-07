#!/usr/bin/env python3
"""Prepare additions to a backed-up shared MBR card; never write a device.

The source prefix must include all existing partitions. New RT-11 partitions
are appended to the extended chain; every old disk and FAT file is checked.
The output includes individual regions and a bounded physical-write plan.
"""
import argparse
import hashlib
import io
import json
import stat
import struct
import sys
from pathlib import Path
from board_common import ROOT
from terak_mbr import partitions, payload, put_entry, align, raw_size

sys.path.insert(0, str(ROOT.parent/'bk0010-01-fpga/tools'))
from prepare_disk_sd import Fat16


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(source, bios, disks, out, capacity, profile=2):
    if not stat.S_ISREG(source.stat().st_mode):
        raise ValueError('Use a regular backup image, never a physical device')
    before = source.read_bytes()
    table = partitions(io.BytesIO(before))
    logical = sorted(n for n in table if n >= 5)
    maximum = 16 if profile == 3 else 12
    if profile not in (2, 3) or not logical or not 1 <= len(disks) <= maximum-logical[-1]:
        raise ValueError(f'Need an existing extended chain with enough free partition numbers (maximum {maximum})')
    if profile == 3 and len(disks) != 1:
        raise ValueError('FB requires one separately prepared boot disk')
    if not table[1] or table[1]['type'] not in (6, 14):
        raise ValueError('Partition 1 must be FAT16')
    boot = bios.read_bytes()
    if len(boot) < 18 or boot[:4] != b'UJTB':
        raise ValueError('Expected a generated TBIOS.BIN')
    size, checksum, version = struct.unpack_from('<III', boot, 4)
    if version != 1 or not size or size > 4096 or size % 2 or size+16 != len(boot) or \
            sum(struct.unpack('<'+'H'*(size//2), boot[16:])) & 65535 != checksum:
        raise ValueError('Invalid BIOS length/version/checksum')
    if len(before) % 512 or len(before)//512 > capacity:
        raise ValueError('Invalid backup/card capacity')
    old_ebrs, current = [], table[4]['start']
    for _ in logical:
        old_ebrs.append(current)
        block = before[current*512:(current+1)*512]
        relative = struct.unpack_from('<I', block, 470)[0]
        if block[466]:
            current = table[4]['start']+relative
    if any((p['start']+p['blocks'])*512 > len(before) for n, p in table.items() if p and n != 4):
        raise ValueError('Backup prefix must contain every existing disk')
    cursor = table[4]['start']+table[4]['blocks']
    additions = []
    for path in disks:
        data = payload(path, 'fd')
        at, start = align(cursor), align(cursor)+2048
        blocks = len(data)//512
        additions.append(dict(partition=logical[-1]+1+len(additions), ebr=at, start=start,
                              blocks=blocks, source=str(path.resolve()), data=data))
        cursor = start+blocks
    if cursor > capacity:
        raise ValueError('RT-11 partitions do not fit the card')
    after = bytearray(before)
    after.extend(bytes(max(0, align(cursor)*512-len(after))))
    regions = []

    def region(name, lba, data):
        if len(data) % 512:
            raise ValueError('Write regions must contain complete sectors')
        old = before[lba*512:lba*512+len(data)]
        after[lba*512:lba*512+len(data)] = data
        regions.append(dict(name=name, start_lba=lba, sectors=len(data)//512, sha256=digest(data),
                            before_sha256=digest(old) if len(old)==len(data) else None))

    base = table[4]['start']
    mbr = bytearray(before[:512])
    struct.pack_into('<I', mbr, 506, cursor-base)
    region('mbr.bin', 0, mbr)
    for i, at in enumerate(old_ebrs):
        block = bytearray(before[at*512:(at+1)*512])
        next_ebr = old_ebrs[i+1] if i+1 < len(old_ebrs) else additions[0]['ebr']
        put_entry(block, 1, 15, next_ebr-base, cursor-next_ebr)
        region(f'ebr-{at}.bin', at, block)
    for i, item in enumerate(additions):
        block = bytearray(512)
        put_entry(block, 0, 218, item['start']-item['ebr'], item['blocks'])
        if i+1 < len(additions):
            next_ebr = additions[i+1]['ebr']
            put_entry(block, 1, 15, next_ebr-base, cursor-next_ebr)
        region(f'ebr-{item["ebr"]}.bin', item['ebr'], block)
        region(f'rt11-partition-{item["partition"]}.bin', item['start'], item['data'])
    fat = table[1]
    volume = Fat16(before[fat['start']*512:(fat['start']+fat['blocks'])*512])
    old_files = volume.files()
    if profile == 2:
        if 'TERAK.CFG' not in old_files or any(name in old_files for name in ('TBIOS.BIN', 'TERAK1.CFG',
                                                                            'TERAK2.CFG', 'TERAK4.CFG')):
            raise ValueError('Expected the direct-boot card without existing primary BIOS profiles')
        configs = {'TERAK1.CFG': old_files['TERAK.CFG'][2]}
        lines = ['version=1']
        for u in range(4):
            lines.append(f'drive{u}={additions[u]["partition"]},fd,rw' if u < len(additions) else f'drive{u}=off')
        configs['TERAK2.CFG'] = ('\n'.join(lines)+'\n').encode()
        # The existing independent memory-test disk remains in its own partition.
        if not table.get(9) or table[9]['type'] != 218 or table[9]['blocks']*512 < raw_size('terak-dsdd'):
            raise ValueError('Expected the existing TERAK memory-test partition 9')
        configs['TERAK4.CFG'] = b'version=1\ndrive0=9,raw,terak-dsdd,ro\ndrive1=off\ndrive2=off\ndrive3=off\n'
        volume.put('TBIOS.BIN', boot)
    else:
        if any(name not in old_files for name in ('TBIOS.BIN', 'TERAK1.CFG', 'TERAK2.CFG', 'TERAK4.CFG')) or \
                'TERAK3.CFG' in old_files or old_files['TBIOS.BIN'][2] != boot:
            raise ValueError('Expected the existing BIOS card with matching TBIOS.BIN and no FB profile')
        if any(not table.get(n) or table[n]['type'] != 218 for n in (11, 12)):
            raise ValueError('Expected RT-11 System Pgms and Extra Files in partitions 11/12')
        configs = {'TERAK3.CFG': (f'version=1\ndrive0={additions[0]["partition"]},fd,rw\n'
                                 'drive1=11,fd,rw\ndrive2=12,fd,rw\ndrive3=off\n').encode()}
    for name, data in configs.items():
        volume.put(name, data)
    new_files = volume.files()
    if any(new_files[name] != value for name, value in old_files.items()):
        raise AssertionError('Existing FAT file or its directory metadata changed')
    region('fat16.bin', fat['start'], volume.data)
    checked = partitions(io.BytesIO(after))
    for n, p in table.items():
        if not p or n == 4:
            continue
        if checked[n] != p or n != 1 and after[p['start']*512:(p['start']+p['blocks'])*512] != \
                before[p['start']*512:(p['start']+p['blocks'])*512]:
            raise AssertionError(f'Existing partition {n} changed')
    for region_info in regions:
        lo, hi = region_info['start_lba'], region_info['start_lba']+region_info['sectors']
        if any(lo < q['start_lba']+q['sectors'] and q['start_lba'] < hi for q in regions
               if q is not region_info):
            raise AssertionError('Overlapping write regions')
    out.mkdir(parents=True, exist_ok=False)
    (out/'card-after.img').write_bytes(after)
    for r in regions:
        (out/r['name']).write_bytes(after[r['start_lba']*512:(r['start_lba']+r['sectors'])*512])
    record = dict(source=str(source.resolve()), source_bytes=len(before), source_sha256=digest(before),
                  card_sectors=capacity, candidate_sha256=digest(after), bios_sha256=digest(boot),
                  preserved_files={name:digest(v[2]) for name,v in old_files.items()},
                  old_partitions=table, partitions=checked, regions=regions,
                  configurations={name:data.decode() for name,data in configs.items()})
    (out/'plan.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--bios', type=Path, required=True)
    parser.add_argument('--disk', type=Path, action='append', required=True)
    parser.add_argument('--card-sectors', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--profile', type=int, choices=(2, 3), default=2,
                        help='2: add initial SJ/BIOS profiles; 3: append a separate FB boot disk')
    args = parser.parse_args()
    result = prepare(args.source, args.bios, args.disk, args.out, args.card_sectors, args.profile)
    print(json.dumps(dict(partitions=result['partitions'], source_sha256=result['source_sha256'],
                         preserved_files=len(result['preserved_files'])), indent=2))
