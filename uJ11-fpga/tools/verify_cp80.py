#!/usr/bin/env python3
"""Verify CP80 software release/archive, reusing immutable CP79 vectors."""
import argparse
import hashlib
import json
import tarfile
from board_common import ROOT
from module_image_cp67 import decode
from verify_cp79 import verify as verify_parent


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(current=False):
    target=ROOT/'tb/reports/cp80';r=json.loads((target/'archive.json').read_text())
    parent_path=ROOT/'tb/reports/cp79/archive.json';parent=json.loads(parent_path.read_text())
    assert sha(parent_path)==r['parent']['archive_sha256']
    assert sha(parent_path.parent/'source.tgz')==r['parent']['source_sha256']
    verify_parent(False)
    assert r['passed'] and not r['installed_on_board']
    for name,digest in r['artifacts'].items():assert sha(target/name)==digest,name
    expected={s['member']:(n,s) for n,s in r['source_files'].items() if 'member' in s};seen=set()
    with tarfile.open(target/'source.tgz','r|gz') as t:
        for member in t:
            assert member.isfile() and member.name in expected and member.name not in seen
            seen.add(member.name);name,entry=expected[member.name];h=hashlib.sha256()
            stream=t.extractfile(member)
            for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
            assert h.hexdigest()==entry['sha256'],name
    assert seen==set(expected)
    checked_refs=set()
    # Master vectors are byte-identical to the qualified CP79 archive. Check
    # every new partition against those bytes; no large duplicate tar member.
    with tarfile.open(parent_path.parent/'source.tgz','r:gz') as t:
        for name,entry in r['source_files'].items():
            if 'parent_source' not in entry:continue
            p=parent['source_files'][entry['parent_source']];assert p['sha256']==entry['sha256']
            refs=sorted(((n,v) for n,v in r['references'].items() if v['master']==name),key=lambda x:x[1]['begin'])
            hashes=[hashlib.sha256() for _ in refs];whole=hashlib.sha256();i=0;count=0
            for count,line in enumerate(t.extractfile(p['member']),1):
                row=count-1;whole.update(line)
                while i<len(refs) and row>=refs[i][1]['end']:i+=1
                assert i<len(refs) and refs[i][1]['begin']<=row<refs[i][1]['end']
                hashes[i].update(line)
            assert whole.hexdigest()==entry['sha256'] and count==refs[-1][1]['end']
            for (n,v),h in zip(refs,hashes):
                assert h.hexdigest()==v['sha256'],n;checked_refs.add(n)
    assert checked_refs==set(r['references'])
    if current:
        for n,s in list(r['source_files'].items())+list(r['references'].items()):assert sha(ROOT/n)==s['sha256'],n
    release=ROOT/'demos/rt11/service/cp80'
    for name,digest in r['release']['files'].items():assert sha(release/name)==digest,name
    for label,file in (('odt','ODT.BIN'),('fp11','FP11.BIN')):
        assert decode((release/file).read_bytes())==r['release']['modules'][label]['format']
    od=r['release']['modules']['odt'];fp=r['release']['modules']['fp11']
    assert od['format']['base']+od['allocation_bytes']<=fp['format']['base']
    assert fp['format']['base']+fp['allocation_bytes']<=0o160000
    assert all(x['passed'] for x in r['results'].values())
    print(json.dumps(dict(passed=True,current_files_checked=current,sources=len(r['source_files']),
                         artifacts=len(r['artifacts']),installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
