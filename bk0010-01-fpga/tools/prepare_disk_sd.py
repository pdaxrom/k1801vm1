#!/usr/bin/env python3
"""Prepare files for adding a BK boot disk to an existing shared SD; no device writes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from models import partitions, parse_bundle, settings

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Fat16:
    def __init__(self, data):
        self.data = bytearray(data)
        sector, self.spc, reserved, copies, entries, total = struct.unpack_from('<HBHBHH', data, 11)
        spf = struct.unpack_from('<H', data, 22)[0]
        if sector != 512 or not self.spc or self.spc & (self.spc-1) or copies != 2 or len(data) != total*512:
            raise ValueError('Expected a bounded FAT16 volume with two FAT copies')
        self.fat = reserved*512
        self.fat_size = spf*512
        self.root = self.fat+copies*self.fat_size
        self.entries = entries
        self.first = self.root+((entries*32+511)//512)*512
        self.cluster_size = self.spc*512
        self.clusters = (len(data)-self.first)//self.cluster_size
        if not 4085 <= self.clusters < 65525 or (self.clusters+2)*2 > self.fat_size or data[510:512] != b'\x55\xaa':
            raise ValueError('Invalid FAT16 layout')
        if data[self.fat:self.fat+self.fat_size] != data[self.fat+self.fat_size:self.root]:
            raise ValueError('FAT copies disagree')

    def chain(self, first):
        result = []
        while first < 0xfff8:
            if not 2 <= first < self.clusters+2 or first in result:
                raise ValueError('Invalid FAT chain')
            result.append(first)
            first = struct.unpack_from('<H', self.data, self.fat+first*2)[0]
        return result

    def files(self):
        result = {}
        for index in range(self.entries):
            offset = self.root+index*32
            entry = self.data[offset:offset+32]
            if entry[0] == 0:
                break
            if entry[0] == 0xe5 or entry[11] & 8:
                continue
            if entry[11] & 16:
                raise ValueError('This preparation requires a flat boot directory')
            name = entry[:8].decode('ascii').rstrip()+'.'+entry[8:11].decode('ascii').rstrip()
            first, size = struct.unpack_from('<HI', entry, 26)
            content = b''.join(self.data[self.first+(c-2)*self.cluster_size:
                                        self.first+(c-1)*self.cluster_size] for c in self.chain(first)) if size else b''
            if len(content) < size or name in result:
                raise ValueError('Truncated file or duplicate directory name')
            result[name] = (offset, bytes(entry), bytes(content[:size]))
        return result

    def put(self, name, content):
        base, ext = name.split('.')
        if not 1 <= len(base) <= 8 or not 1 <= len(ext) <= 3 or name != name.upper() or not content:
            raise ValueError('Expected a nonempty uppercase 8.3 file')
        existing = self.files().get(name)
        if existing and existing[2] == content:
            return
        if existing:
            offset, raw, _ = existing
            if raw[11] & 1:
                raise ValueError(f'Read-only file: {name}')
            old = self.chain(struct.unpack_from('<H', raw, 26)[0])
        else:
            offset = next((self.root+i*32 for i in range(self.entries)
                           if self.data[self.root+i*32] in (0, 0xe5)), None)
            if offset is None:
                raise ValueError('Root directory full')
            raw, old = bytes(32), []
        count = (len(content)+self.cluster_size-1)//self.cluster_size
        free = [c for c in range(2, self.clusters+2)
                if struct.unpack_from('<H', self.data, self.fat+c*2)[0] == 0]
        if len(free) < count:
            raise ValueError('FAT16 has insufficient free clusters')
        chain = free[:count]
        for index, cluster in enumerate(chain):
            position = self.first+(cluster-2)*self.cluster_size
            chunk = content[index*self.cluster_size:(index+1)*self.cluster_size]
            self.data[position:position+self.cluster_size] = chunk.ljust(self.cluster_size, b'\0')
        for copy in range(2):
            table = self.fat+copy*self.fat_size
            for cluster in old:
                struct.pack_into('<H', self.data, table+cluster*2, 0)
            for index, cluster in enumerate(chain):
                struct.pack_into('<H', self.data, table+cluster*2, chain[index+1] if index+1 < count else 0xffff)
        entry = bytearray(raw)
        entry[:11] = (base.ljust(8)+ext.ljust(3)).encode('ascii')
        entry[11] = 0x20
        struct.pack_into('<HI', entry, 26, chain[0], len(content))
        self.data[offset:offset+32] = entry


def prepare(mbr, boot, disk, capacity, out, second=None, start_lba=None):
    table = partitions(mbr, capacity)
    if any(mbr[494:510]):
        raise ValueError('Partition 4 is already occupied')
    if table[0]['length']*512 != len(boot) or len(disk) != 1600*512:
        raise ValueError('Wrong boot volume size or disk must be exactly 800 KiB')
    start = ((max(p['start']+p['length'] for p in table if p)+2047)//2048)*2048
    if start_lba is not None:
        if start_lba < start or start_lba % 2048:
            raise ValueError('The new extent must be aligned and beyond existing partitions')
        start = start_lba
    after = bytearray(mbr)
    ebrs = {}
    regions = []
    if second is None:
        struct.pack_into('<B3sB3sII',after,494,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',start,1600)
        disks = {'bk-disk.bin':(start,disk)}
        selected = (4,0,0,0)
        readonly = 1
        end = start+1600
        checked = partitions(after,capacity)
    else:
        if len(second) != 1600*512:
            raise ValueError('Second disk must also be exactly 800 KiB')
        first_data, next_ebr, second_data = start+2048,start+4096,start+6144
        end = second_data+1600
        struct.pack_into('<B3sB3sII',after,494,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',start,end-start)
        for lba,data_lba in ((start,first_data),(next_ebr,second_data)):
            sector = bytearray(512)
            sector[510:] = b'\x55\xaa'
            struct.pack_into('<B3sB3sII',sector,446,0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',data_lba-lba,1600)
            if lba == start:
                struct.pack_into('<B3sB3sII',sector,462,0,b'\xfe\xff\xff',0x0f,b'\xfe\xff\xff',next_ebr-start,end-next_ebr)
            ebrs[lba] = bytes(sector)
        checked = partitions(after,capacity,ebrs.__getitem__)
        disks = {'bk-disk-5.bin':(first_data,disk),'bk-disk-6.bin':(second_data,second)}
        selected, readonly = (6,5,0,0),3
    volume = Fat16(boot)
    before_files = volume.files()
    loader = before_files.get('LOADERBK.BIN')
    if loader:
        previous = before_files.get('LDRBKOLD.BAK')
        if previous and previous[2] != loader[2]:
            raise ValueError('Existing loader backup differs')
        volume.put('LDRBKOLD.BAK', loader[2])
    for name in ('LOADERBK.BIN', *(f'BK{i:02}.ROM' for i in range(5))):
        data = (ROOT/'build'/name).read_bytes()
        if name.endswith('.ROM'):
            parse_bundle(data, int(name[2:4]))
        volume.put(name, data)
    volume.put('BK.SET', settings(model=3, drives=selected, readonly=readonly))
    after_files = volume.files()
    own = {'LOADERBK.BIN', 'BK.SET', *(f'BK{i:02}.ROM' for i in range(5))}
    for name, original in before_files.items():
        if name not in own and after_files[name] != original:
            raise ValueError(f'Existing file changed: {name}')
    out.mkdir(parents=True, exist_ok=False)
    payloads = {'mbr-after.bin':bytes(after),'fat16-after.bin':bytes(volume.data)}
    for name,(lba,data) in disks.items():
        payloads[name] = data
        regions.append({'name':name,'start_lba':lba,'sectors':len(data)//512,'kind':'disk'})
    for index,(lba,data) in enumerate(ebrs.items(),1):
        name = f'ebr-{index}.bin'
        payloads[name] = data
        regions.append({'name':name,'start_lba':lba,'sectors':1,'kind':'ebr'})
    simulation = bytearray(((end+1023)//1024)*1024*512)
    simulation[:512] = after
    base = table[0]['start']*512
    simulation[base:base+len(boot)] = volume.data
    for region in regions:
        lba = region['start_lba']
        simulation[lba*512:(lba+region['sectors'])*512] = payloads[region['name']]
    payloads['simulation-sd.img'] = simulation
    for name, content in payloads.items():
        (out/name).write_bytes(content)
    result = {'partition':4,'start_lba':start,'sectors':end-start,'card_sectors':capacity,
              'partitions':checked,'regions':regions,
              'settings':{'model':3,'frequency':4,'color':0,'drives':list(selected),'readonly':readonly},
              'mbr_before_sha256':sha(mbr), 'fat16_before_sha256':sha(boot),
              'payloads':{name:sha(data) for name,data in payloads.items()},
              'preserved_files':{name:sha(entry[2]) for name,entry in before_files.items() if name not in own},
              'loader_backup':'LDRBKOLD.BAK' if loader else None,
              'device_written':False}
    (out/'prepared.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mbr',type=Path,required=True)
    parser.add_argument('--boot-volume',type=Path,required=True)
    parser.add_argument('--disk',type=Path,required=True)
    parser.add_argument('--second-disk',type=Path)
    parser.add_argument('--start-lba',type=int)
    parser.add_argument('--card-sectors',type=int,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.mbr.read_bytes(), args.boot_volume.read_bytes(), args.disk.read_bytes(),
                             args.card_sectors,args.out,args.second_disk.read_bytes() if args.second_disk else None,args.start_lba), indent=2))


if __name__ == '__main__':
    main()
