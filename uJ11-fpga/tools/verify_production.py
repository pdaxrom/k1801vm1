#!/usr/bin/env python3
"""Verify cleanup equivalence to the programmed hardware and installed modules.

This is a release-baseline check, not a replacement for functional tests or
synthesis after future RTL changes. No Git checkout or historical builder is used.
"""
import hashlib
import json
import re
import tarfile
from pathlib import Path
from board_common import ROOT


def digest(data):return hashlib.sha256(data).hexdigest()


def tokens(data, microcode=False):
    text=data.decode().replace('build/cp67-modules/','build/hardware/')
    if microcode:text=re.sub(r';[^\n]*','',text)
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    return re.sub(r'\s+','',text)


def main():
    release=ROOT/'releases/hc1200'
    manifest=json.loads((release/'inputs.json').read_text())
    record=json.loads((ROOT/'tests/baseline/production.json').read_text())
    with tarfile.open(release/'source.tgz') as archive:
        for path,want in manifest['files'].items():
            if path.startswith('generated:'):continue
            data=archive.extractfile(path).read()
            assert digest(data)==want,path
        for row in record['sources']:
            old=archive.extractfile(row['archive_path']).read()
            current=(ROOT/row['path']).read_bytes()
            assert digest(old)==row['sha256'],row['path']
            assert tokens(current,row['path'].endswith('.uasm'))==tokens(old,row['path'].endswith('.uasm')),row['path']
        for path in ('uj11_m0_ebr.v','src/microcode/generated/uj11_decode_table.v',
                     'src/microcode/generated/uj11_firmware_rom.v'):
            old=archive.extractfile('build/cp67-modules/'+path).read()
            assert tokens((ROOT/'build/hardware'/Path(path).name).read_bytes())==tokens(old),path
    for output,baseline in (('m0','microcode'),('decode','decode'),('firmware','firmware')):
        actual=[int(w,16) for w in (ROOT/f'build/hardware/{output}.mem').read_text().split()]
        expected=[int(w,16) for w in (ROOT/f'tests/baseline/{baseline}.mem').read_text().split()]
        assert actual==expected,output
    expected=json.loads((ROOT/'releases/software/manifest.json').read_text())
    outputs={'ODT.BIN':'build/software/odt/ODT.BIN','SDBOOT.BIN':'build/software/sdboot/SDBOOT.BIN',
             'UJMOD.SAV':'build/software/loader/UJMOD.SAV','FP11.BIN':'build/fpp/software/FP11.BIN'}
    for name,item in expected['files'].items():
        data=(ROOT/'releases/software'/name).read_bytes()
        assert len(data)==item['bytes'] and digest(data)==item['sha256'],name
        if name in outputs:assert (ROOT/outputs[name]).read_bytes()==data,name
    jed=json.loads((release/'jed.json').read_text())
    assert digest((release/'design.jed').read_bytes())==jed['sha256']
    basic=ROOT/'releases/basic'
    for name,item in json.loads((basic/'manifest.json').read_text())['files'].items():
        assert digest((basic/name).read_bytes())==item['sha256'],name
    print('PASS production: RTL/EBR token identity, three exact ROM images, four rebuilt modules, released BASIC and JED')


if __name__=='__main__':main()
