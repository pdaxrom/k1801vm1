#!/usr/bin/env python3
"""Reproducible HC7000 RT-11XM kit; never opens a physical SD device."""
import argparse
from datetime import date
import gzip
import json
from pathlib import Path
import struct
import subprocess
from board_common import ROOT
from build_sd_image import BASE, BASE_SHA, sha, directory, text_bytes
from build_hgx import build as build_utilities

NAME = 'uj11-hc7000-rt11xm.img'
RELEASE_DATE = '2026-09-27'


def build(out, release_date=RELEASE_DATE):
    out = out.resolve();out.mkdir(parents=True,exist_ok=False)
    native = out/'software'
    software = build_utilities(native)
    assert not software['fixtures']
    staging = out/'files';staging.mkdir()
    source = BASE.read_bytes();assert sha(source) == BASE_SHA
    original,_,_ = directory(source)
    additions = {};inputs = {str(BASE.relative_to(ROOT.parent)):BASE_SHA}

    def add(path, content=None):
        payload = path.read_bytes() if content is None else content
        assert path.name not in additions
        (staging/path.name).write_bytes(payload)
        relative = str(path.relative_to(ROOT))
        inputs[relative] = sha(path.read_bytes())
        additions[path.name] = dict(source=relative,bytes=len(payload),sha256=sha(payload))

    manifest = ROOT/'releases/basic/manifest.json'
    inputs[str(manifest.relative_to(ROOT))] = sha(manifest.read_bytes())
    basic = json.loads(manifest.read_text())['files']
    for name in ('B81FPU.SAV','B81FPD.SAV'):
        path = manifest.parent/name
        assert sha(path.read_bytes()) == basic[name]['sha256']
        add(path)
    for name in ('HGX.SYS','HGTIME.SAV','TMRATE.SAV'):
        path = native/name
        assert sha(path.read_bytes()) == software['outputs'][name]
        add(path)
    for folder in ('demos/rt11/basic','demos/rt11/sd-hc7000'):
        for path in sorted((ROOT/folder).iterdir()):
            if path.suffix in ('.BAS','.TXT','.COM'):
                add(path,text_bytes(path))
    for name in software['sources']:
        path = ROOT/name;add(path,text_bytes(path))
    inputs['tools/build_sd_mmu.py'] = sha(Path(__file__).read_bytes())
    inputs['tools/build_sd_image.py'] = sha((ROOT/'tools/build_sd_image.py').read_bytes())
    inputs.update(software['scripts'])
    image = out/NAME
    blob = bytearray(source)
    blob[:3072] = (native/'xm-boot.bin').read_bytes()
    image.write_bytes(blob)
    rt = ROOT.parent/'lsi11/rt11tool'
    commands = []

    def tool(*args):
        result = subprocess.run([str(rt),*map(str,args)],check=True,capture_output=True,text=True)
        commands.append(dict(args=list(map(str,args)),stdout=result.stdout,stderr=result.stderr))
        return result.stdout

    tool('rm',image,'STARTX.COM')
    for name in sorted(additions):
        tool('add',image,staging/name,name)
    stamp = date.fromisoformat(release_date);assert 1972<=stamp.year<=2035
    age = stamp.year-1972
    word = ((age//32)<<14)|(stamp.month<<10)|(stamp.day<<5)|(age%32)
    blob = bytearray(image.read_bytes());entries,free,_ = directory(blob)
    for name in additions:
        struct.pack_into('<H',blob,entries[name]['entry_offset']+12,word)
    for start,count in free:
        blob[start*512:(start+count)*512] = bytes(count*512)
    image.write_bytes(blob)
    entries,_,geometry = directory(blob)
    assert blob[512:1024] == source[512:1024]
    assert blob[-66*512:] == source[-66*512:]
    for name,info in original.items():
        if name != 'STARTX.COM':
            assert entries[name]['sha256'] == info['sha256'],name
            assert entries[name]['start_block'] == info['start_block'],name
    extracted = out/'extracted';extracted.mkdir()
    for name,info in additions.items():
        tool('extract',image,extracted,name)
        payload = (staging/name).read_bytes()
        padded = payload+bytes((-len(payload))%512)
        assert (extracted/name).read_bytes() == padded
        assert entries[name]['sha256'] == sha(padded)
        info.update(image_sha256=sha(padded),blocks=len(padded)//512)
    compressed = out/(NAME+'.gz')
    with compressed.open('wb') as stream:
        with gzip.GzipFile(filename='',mode='wb',fileobj=stream,mtime=0,compresslevel=9) as gz:
            gz.write(blob)
    assert gzip.decompress(compressed.read_bytes()) == blob
    listing = tool('ls',image,'--long')
    (out/'directory.txt').write_text(''.join(line.rstrip()+'\n' for line in listing.splitlines()))
    (out/'image-info.txt').write_text(tool('info',image))
    report = dict(release_date=release_date,board='hc7000-lcd-sram',cpu='mmu',
        monitor='RT-11XM (S) V05.03',format='raw RK07 at SD LBA 0; no MBR/FAT',
        image=dict(name=NAME,bytes=len(blob),sectors=len(blob)//512,sha256=sha(blob)),
        compressed=dict(name=compressed.name,bytes=compressed.stat().st_size,sha256=sha(compressed.read_bytes())),
        base_sha256=BASE_SHA,inputs=inputs,software=software,files=additions,directory=entries,
        geometry=geometry,zeroed_free_extents=True,
        verification=dict(directory_and_extents=True,extracted_files=True,
            preserved_distribution_except_startx=True,home_and_reserved_tail_unchanged=True,
            xm_boot_blocks='native COPY/BOOT SY:RT11XM.SYS SY:',decompression=True))
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'packaging.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'SHA256SUMS').write_text(f'{sha(blob)}  {NAME}\n{sha(compressed.read_bytes())}  {compressed.name}\n')
    assert sha(BASE.read_bytes()) == BASE_SHA
    print(json.dumps({k:report[k] for k in ('image','compressed','geometry')},indent=2))
    return report


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/sd-hc7000')
    p.add_argument('--date',default=RELEASE_DATE)
    a=p.parse_args();build(a.out,a.date)
