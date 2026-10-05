#!/usr/bin/env python3
"""Apply a checked BK disk preparation to its original Linux SD; preserve P601 partitions."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_at(device, lba, length):
    device.seek(lba*512)
    data = device.read(length)
    if len(data) != length:
        raise ValueError('Short SD read')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run',action='store_true')
    parser.add_argument('--overwrite-unallocated',action='store_true')
    parser.add_argument('--device',type=Path,required=True)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--validation',type=Path,required=True)
    parser.add_argument('--validation-record',type=Path,required=True)
    parser.add_argument('--prepared-directory',type=Path,required=True)
    args = parser.parse_args()
    if str(args.device) != '/dev/disk/by-id/usb-Generic_MassStorageClass_000000001210-0:2':
        raise ValueError('This recorded session targets only the verified SD reader slot')
    if not stat.S_ISBLK(args.device.stat().st_mode):
        raise ValueError('Expected the original block device')
    resolved = str(args.device.resolve())
    for line in Path('/proc/mounts').read_text().splitlines():
        source = os.path.realpath(line.split()[0])
        if source == resolved or source.startswith(resolved) and source[len(resolved):].isdigit():
            raise ValueError('Unmount all SD partitions before applying this preparation')
    validation = args.validation.read_text()
    if 'PASS disk OS cold boot: native CPU/BIOS/ROM/KNGMD/raw partition/SPI; VRAM matches C' not in validation:
        raise ValueError('Native OS boot verification has not passed')
    root = args.directory
    prepared = args.prepared_directory
    description = json.loads((prepared/'prepared.json').read_text())
    checked = json.loads(args.validation_record.read_text())
    regions = description.get('regions',[{'name':'bk-disk.bin','start_lba':description['start_lba'],
                                         'sectors':description['sectors'],'kind':'disk'}])
    expected_disks = {item['name']:description['payloads'][item['name']] for item in regions if item['kind']=='disk'}
    tested_disks = checked.get('disks',{'bk-disk.bin':checked.get('disk_sha256')})
    if (checked.get('passed') is not True or tested_disks != expected_disks or
            checked.get('simulation_sha256') != description['payloads']['simulation-sd.img'] or
            checked.get('native_log_sha256') != sha(args.validation.read_bytes())):
        raise ValueError('The successful native test did not use this prepared SD and disks')
    payloads = {name:(prepared/name).read_bytes() for name in description['payloads'] if name != 'simulation-sd.img'}
    for name, data in payloads.items():
        if sha(data) != description['payloads'][name]:
            raise ValueError(f'Prepared payload changed: {name}')
    originals = {name:(root/name).read_bytes() for name in
                 ('mbr-before.bin','fat16-before.bin','partition2-before.bin','partition3-before.bin','partition4-before.bin')}
    mbr, after = originals['mbr-before.bin'], payloads['mbr-after.bin']
    if len(mbr) != 512 or after[:494] != mbr[:494] or after[510:] != mbr[510:] or any(mbr[494:510]):
        raise ValueError('Preparation must only add the previously empty fourth MBR entry')
    if sha(mbr) != description['mbr_before_sha256'] or sha(originals['fat16-before.bin']) != description['fat16_before_sha256']:
        raise ValueError('Preparation and backups disagree')

    boot_base, boot_sectors = struct.unpack_from('<II',mbr,454)
    start, length = struct.unpack_from('<II',after,502)
    if start != description['start_lba'] or length != description['sectors']:
        raise ValueError('Unexpected BK disk extent')
    if length == 1600:
        free_before = originals['partition4-before.bin']
    else:
        info = json.loads((root/'extended-before.json').read_text())
        free_before = (root/'extended-before.bin').read_bytes()
        if (start,length) != (info['start_lba'],info['sectors']) or sha(free_before) != info['sha256']:
            raise ValueError('Extended-region backup disagrees')
    if len(free_before) != length*512 or any(free_before) and not args.overwrite_unallocated:
        raise ValueError('Nonempty unallocated data needs explicit overwrite authorization')
    occupied = []
    for item in regions:
        base,size = item['start_lba'],item['sectors']
        if base < start or base+size > start+length or len(payloads[item['name']]) != size*512:
            raise ValueError('Prepared region exceeds the new extent')
        if any(base < other+count and other < base+size for other,count in occupied):
            raise ValueError('Prepared regions overlap')
        occupied.append((base,size))
    boot_before, boot_after = originals['fat16-before.bin'],payloads['fat16-after.bin']
    if len(boot_before) != boot_sectors*512 or len(boot_after) != len(boot_before):
        raise ValueError('Boot volume size changed')
    reserved = struct.unpack_from('<H',boot_before,14)[0]
    copies = boot_before[16]
    spf = struct.unpack_from('<H',boot_before,22)[0]
    root_start = reserved+copies*spf
    root_end = root_start+(struct.unpack_from('<H',boot_before,17)[0]*32+511)//512
    changed = [sector for sector in range(boot_sectors)
               if boot_before[sector*512:(sector+1)*512] != boot_after[sector*512:(sector+1)*512]]
    if any(sector < reserved for sector in changed):
        raise ValueError('Preparation changed the FAT16 boot sector')
    result = {'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'device':str(args.device),'resolved_device':resolved,'written':False,
              'preparation':description,'validation_sha256':sha(args.validation.read_bytes()),
              'validation_record':checked,'prepared_directory':str(prepared),
              'fpga_programmed':False,'physical_boot_verified':False,
              'backup_directory':str(root),'protected_partitions':{},'completed_actions':[]}
    report = root/('sd-preflight.json' if args.dry_run else 'sd-written.json')

    def save():
        report.write_text(json.dumps(result,indent=2)+'\n')

    with args.device.open('rb' if args.dry_run else 'r+b',buffering=0) as device:
        fcntl.flock(device,fcntl.LOCK_EX|fcntl.LOCK_NB)
        capacity = struct.unpack('Q',fcntl.ioctl(device,0x80081272,bytes(8)))[0]//512
        if capacity != description['card_sectors']:
            raise ValueError('SD capacity changed')
        if read_at(device,0,512) != mbr or read_at(device,boot_base,boot_sectors*512) != originals['fat16-before.bin']:
            raise ValueError('SD changed after backup/preparation')
        if read_at(device,start,length*512) != free_before:
            raise ValueError('The new partition region changed')
        for partition in (2,3):
            base,size = struct.unpack_from('<II',mbr,446+(partition-1)*16+8)
            data = read_at(device,base,size*512)
            if data != originals[f'partition{partition}-before.bin']:
                raise ValueError('A protected P601 partition changed')
            result['protected_partitions'][str(partition)] = {'start':base,'sectors':size,'before_sha256':sha(data)}
        save()
        if args.dry_run:
            result.update(preflight_verified=True,changed_boot_sectors=len(changed),
                          finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
            save()
            print(json.dumps(result,indent=2))
            return

        def write_at(lba,data,label):
            device.seek(lba*512)
            if device.write(data) != len(data):
                raise ValueError('Short SD write')
            os.fsync(device.fileno())
            if read_at(device,lba,len(data)) != data:
                raise ValueError('SD readback differs')
            result['completed_actions'].append(label)
            save()

        # Store the disk and new file data first. Publish directory entries and
        # finally the fourth partition entry only after their bytes are durable.
        free_after = bytearray(free_before)
        for item in regions:
            data = payloads[item['name']]
            write_at(item['start_lba'],data,item['name']+'-and-readback')
            offset = (item['start_lba']-start)*512
            free_after[offset:offset+len(data)] = data
        if read_at(device,start,length*512) != free_after:
            raise ValueError('New extent readback differs')
        for region,sectors in [('file-data',[x for x in changed if x >= root_end]),
                              ('fat-copies',[x for x in changed if reserved <= x < root_start]),
                              ('directory',[x for x in changed if root_start <= x < root_end])]:
            for sector in sectors:
                device.seek((boot_base+sector)*512)
                if device.write(boot_after[sector*512:(sector+1)*512]) != 512:
                    raise ValueError('Short boot volume write')
            os.fsync(device.fileno())
            result['completed_actions'].append(region)
            save()
        if read_at(device,boot_base,boot_sectors*512) != boot_after:
            raise ValueError('Boot volume readback differs')
        write_at(0,after,'mbr-and-readback')
        for partition,info in result['protected_partitions'].items():
            info['after_sha256'] = sha(read_at(device,info['start'],info['sectors']*512))
            if info['after_sha256'] != info['before_sha256']:
                raise ValueError('Protected P601 partition was modified')
        result.update(written=True,boot_changed_sectors=len(changed),
                      mbr_sha256=sha(read_at(device,0,512)),
                      fat16_sha256=sha(read_at(device,boot_base,boot_sectors*512)),
                      extent_sha256=sha(read_at(device,start,length*512)),
                      disk_sha256s={item['name']:sha(read_at(device,item['start_lba'],item['sectors']*512))
                                    for item in regions if item['kind']=='disk'},
                      finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        save()
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
