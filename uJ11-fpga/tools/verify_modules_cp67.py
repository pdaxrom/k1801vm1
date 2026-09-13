#!/usr/bin/env python3
"""Verify immutable CP67 test/synthesis/JED artifacts, optionally current files."""
import argparse
import hashlib
import json
import tarfile
from pathlib import Path
from board_common import ROOT


def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(current=False):
    target=ROOT/'tb/reports/cp67'
    archive=json.loads((target/'archive.json').read_text());assert archive['passed']
    for name,h in archive['artifacts'].items():assert digest(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for name,h in archive['source_files'].items():
            assert hashlib.sha256(t.extractfile(name).read()).hexdigest()==h,name
            if current:assert digest(ROOT/name)==h,name
    for name,h in archive['hardware']['files'].items():assert digest(ROOT/name)==h,name
    measured=json.loads((ROOT/'synth/reports/cp67b/result.json').read_text())
    assert measured['fully_routed'] and measured['timing_pass'] and measured['trace_timing_pass']
    assert measured['applied_clock_mhz']==31.824 and measured['microcode_words']==1005 and not measured['mmu']
    assert (measured['lut4'],measured['ff'],measured['ebr'])==(1244,381,7)
    release=ROOT/'synth/releases/cp67b';jed=json.loads((release/'jed.json').read_text())
    inputs=json.loads((ROOT/'synth/reports/cp67b/inputs.json').read_text())
    assert jed['input_revision_sha256']==inputs['input_revision_sha256']
    assert digest(release/'design.jed')==jed['sha256']
    assert ('\nC'+jed['checksum']+'*') in (release/'design.jed').read_text()
    software=ROOT/'demos/rt11/service/cp67'
    record=json.loads((software/'release.json').read_text())
    for name,h in record['files'].items():assert digest(software/name)==h,name
    assert record['abi']==3 and not record['relocatable']
    print(json.dumps(dict(passed=True,current_files_checked=current,counts=archive['counts'],jed_sha256=jed['sha256'],programmed=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--current',action='store_true')
    verify(p.parse_args().current)
