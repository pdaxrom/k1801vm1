#!/usr/bin/env python3
"""Retain CP75 fixtures; allow only the documented LDF FIUV flags correction."""
import hashlib
import json
import tarfile
from board_common import ROOT
from build_fp11_cp76 import OUT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run():
    old=ROOT/'tb/reports/cp75';manifest=json.loads((old/'archive.json').read_text())
    oldhash=sha(old/'source.tgz');assert oldhash==manifest['artifacts']['source.tgz']
    with tarfile.open(old/'source.tgz') as t:
        a=t.extractfile('project/build/cp75-fp11/sync/vectors.txt').read().splitlines()
    b=(OUT/'sync/vectors.txt').read_bytes().splitlines()
    assert len(a)==225084 and len(b)==444444
    unchanged=corrected=0
    for oldrow,newrow in zip(a,b):
        x=[int(v,16) for v in oldrow.split()];y=[int(v,16) for v in newrow.split()]
        differences={i for i in range(129) if x[i]!=y[i]}
        if not differences:unchanged+=1;continue
        assert differences<={48,128},(x[79],differences)
        assert x[0]&0o177400==0o172400 and x[0]&0o70 and x[1]&0o4000
        assert x[50]==0o14 and x[51]==0o1000 and y[128]==5 and x[128]==0
        assert y[48]==(x[48]&~15)|12
        corrected+=1
    files=(old/'archive.json',OUT/'sync/vectors.txt',ROOT/'tools/compare_vectors_fp76.py')
    result=dict(passed=True,previous_cases=len(a),unchanged_previous_cases=unchanged,
                corrected_ldf_fiuv_cases=corrected,current_cases=len(b),new_cases=len(b)-len(a),
                permitted_changes='LDF memory FIUV: field 48 FN/FZ=1, FV/FC=0; field 128 manual marker=5. All other fields, including IDs, unchanged.',
                previous_source_sha256=oldhash,inputs={str(p.relative_to(ROOT)):sha(p) for p in files})
    (OUT/'baseline.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':run()
