#!/usr/bin/env python3
"""Verify CP79 immutable archive/release hashes and optional current sources."""
import argparse
import hashlib
import json
import tarfile
from board_common import ROOT
from module_image_cp67 import decode

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def verify(current=False):
    target=ROOT/'tb/reports/cp79';r=json.loads((target/'archive.json').read_text())
    assert r['passed'] and not r['installed_on_board']
    for name,digest in r['artifacts'].items():assert sha(target/name)==digest,name
    expected={s['member']:(n,s) for n,s in r['source_files'].items()};seen=set();checked_refs=set()
    # Stream the large vector archive once. Partition vectors are exact line
    # slices of the master, with independent hashes; avoid storing duplicates.
    with tarfile.open(target/'source.tgz','r|gz') as t:
        for member in t:
            assert member.isfile() and member.name in expected and member.name not in seen
            seen.add(member.name);name,entry=expected[member.name];h=hashlib.sha256()
            refs=sorted(((n,v) for n,v in r['references'].items() if v['master']==name),key=lambda x:x[1]['begin'])
            stream=t.extractfile(member)
            if refs:
                hashes=[hashlib.sha256() for _ in refs];i=0;count=0
                for count,line in enumerate(stream,1):
                    row=count-1
                    while i<len(refs) and row>=refs[i][1]['end']:i+=1
                    assert i<len(refs) and refs[i][1]['begin']<=row<refs[i][1]['end']
                    h.update(line);hashes[i].update(line)
                assert count==refs[-1][1]['end']
                for (n,v),part_hash in zip(refs,hashes):
                    assert part_hash.hexdigest()==v['sha256'],n;checked_refs.add(n)
                    if current:assert sha(ROOT/n)==v['sha256'],n
            else:
                for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
            assert h.hexdigest()==entry['sha256'],name
            if current:assert sha(ROOT/name)==entry['sha256'],name
    assert seen==set(expected) and checked_refs==set(r['references'])
    release=ROOT/'demos/rt11/service/cp79'
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
