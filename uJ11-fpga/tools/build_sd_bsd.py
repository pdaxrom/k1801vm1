#!/usr/bin/env python3
"""Create a partitioned 2.9BSD SD file: RL0 root, RL1 swap, XP0 usr, boot RL0."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
from board_common import ROOT
from build_storage_menu import build as build_menu
from sdcard import Label, MENU, add_partition, publish, copy_bytes, read_label
from storage_menu import unpack

NAME = 'uj11-hc7000-2.9bsd.img'
INPUTS = (('rl02', 0, '2.9BSD-root.rl02', 'bsd-root'),
          ('rl02', 1, 'swap.rl02', 'bsd-swap'),
          ('rm05', 0, '2.9BSD-usr.rm05', 'bsd-usr'))

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def build(out, disks, compress=True):
    out = out.resolve();out.mkdir(parents=True, exist_ok=False)
    label = Label(1048576, features=MENU)
    hashes = {}
    for media, unit, filename, name in INPUTS:
        label = add_partition(label, media, unit, boot=name=='bsd-root', name=name)
        src = disks/filename
        if not 0 < src.stat().st_size <= label.partitions[-1].blocks*512 or src.stat().st_size % 512:
            raise ValueError(f'{src}: size does not match {media}')
        hashes[filename] = digest(src)
    menu_record = build_menu(out/'menu')
    menu = (out/'menu/menu.img').read_bytes();unpack(menu)
    image = out/NAME
    with image.open('x+b') as f:
        f.truncate(label.blocks*512)
        f.seek(2*512);f.write(menu)
        for p, (_, _, filename, _) in zip(label.partitions, INPUTS):
            f.seek(p.start*512)
            with (disks/filename).open('rb') as src:
                copy_bytes(src, f, (disks/filename).stat().st_size)
        publish(f, label)
        assert read_label(f)[0] == label
        for p, (_, _, filename, _) in zip(label.partitions, INPUTS):
            f.seek(p.start*512);h=hashlib.sha256();left=(disks/filename).stat().st_size
            while left:
                b=f.read(min(left,1024*1024));h.update(b);left-=len(b)
            assert h.hexdigest() == hashes[filename]
            assert not any(f.read(p.blocks*512-(disks/filename).stat().st_size))
    record = dict(board='hc7000-lcd-sram', cpu='mmu', fpp='off', iop='storage',
        format='UJ11SD v1; duplicate label, SD boot menu, fixed-size disk partitions',
        default_boot='rl0', boot_command='rl(0,0)rlunix',
        partitions=[vars(p) for p in label.partitions],
        sources={str(disks/f):dict(sha256=h,bytes=(disks/f).stat().st_size,
            zero_padding=p.blocks*512-(disks/f).stat().st_size)
            for p,(f,h) in zip(label.partitions,hashes.items())}, menu=menu_record,
        image=dict(name=NAME, bytes=image.stat().st_size, sha256=digest(image)),
        verification=dict(partition_bytes_match_originals=True, originals_unchanged=True))
    if compress:
        compressed=out/(NAME+'.gz')
        with image.open('rb') as src, compressed.open('xb') as dst:
            with gzip.GzipFile(filename='',fileobj=dst,mode='wb',mtime=0,compresslevel=9) as gz:
                shutil.copyfileobj(src,gz)
        with gzip.open(compressed,'rb') as f:
            assert hashlib.file_digest(f,'sha256').hexdigest()==record['image']['sha256']
        record['compressed']=dict(name=compressed.name,bytes=compressed.stat().st_size,sha256=digest(compressed))
    for f,h in hashes.items():assert digest(disks/f)==h
    (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    (out/'SHA256SUMS').write_text(''.join(f"{record[k]['sha256']}  {record[k]['name']}\n" for k in ('image','compressed') if k in record))
    print(json.dumps(record['image'],indent=2))
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/sd-bsd')
    p.add_argument('--disks',type=Path,default=ROOT.parent/'lsi11/disks/bsd2.9')
    p.add_argument('--no-compress',action='store_true')
    a=p.parse_args();build(a.out,a.disks.resolve(),not a.no_compress)
