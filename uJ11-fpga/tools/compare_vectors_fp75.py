#!/usr/bin/env python3
"""Ensure every CP74 fixture is retained unchanged, apart from its case ID."""
import hashlib
import json
import tarfile
from collections import Counter
from board_common import ROOT
from build_fp11_cp75 import OUT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def fingerprint(row):
    fields=row.split();assert len(fields)==129
    return hashlib.sha256(b' '.join(fields[:79]+fields[80:])).digest()


def run():
    old=ROOT/'tb/reports/cp74';manifest=json.loads((old/'archive.json').read_text())
    oldhash=sha(old/'source.tgz');assert oldhash==manifest['artifacts']['source.tgz']
    with tarfile.open(old/'source.tgz') as t:
        a=Counter(fingerprint(row) for row in t.extractfile('project/build/cp74-fp11/sync/vectors.txt'))
    with (OUT/'sync/vectors.txt').open('rb') as f:b=Counter(fingerprint(row) for row in f)
    assert not a-b,'previous fixture data/expected state changed'
    previous=sum(a.values());current=sum(b.values())
    assert previous==194396 and current==225084
    files=(old/'archive.json',OUT/'sync/vectors.txt',ROOT/'tools/compare_vectors_fp75.py')
    result=dict(passed=True,unchanged_previous_cases=previous,current_cases=current,new_cases=current-previous,
                ignored_field='79: case ID only',previous_source_sha256=oldhash,
                inputs={str(p.relative_to(ROOT)):sha(p) for p in files})
    (OUT/'baseline.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':run()
