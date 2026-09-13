#!/usr/bin/env python3
"""Verify frozen CP68 inputs/results/package; optionally compare live sources."""
import argparse
import csv
import hashlib
import json
import tarfile
from board_common import ROOT
from module_image_cp67 import decode


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(current=False):
    target=ROOT/'tb/reports/cp68';record=json.loads((target/'archive.json').read_text())
    assert record['passed'] and not record['installed_on_board']
    for name,h in record['artifacts'].items():assert sha(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for name,m in record['source_files'].items():
            assert hashlib.sha256(t.extractfile(m['member']).read()).hexdigest()==m['sha256'],name
            if current:assert sha(ROOT/name)==m['sha256'],name
    for name,h in record['hardware']['files'].items():assert sha(ROOT/name)==h,name
    release=ROOT/'demos/rt11/service/cp68';manifest=json.loads((release/'release.json').read_text())
    for name,h in manifest['files'].items():assert sha(release/name)==h,name
    assert decode((release/'FP11.BIN').read_bytes())==manifest['format']
    assert manifest['allocation_bytes']==668 and manifest['supported_encodings']==21
    assert '90112 oracle cases / 5857280 checks' in (target/'sync/simulation.log').read_text()
    for mode in ('logic','vendor'):
        assert '351 oracle cases / 22815 checks' in (target/mode/'simulation.log').read_text()
    for mode in ('events-sync','events-vendor'):
        assert '28 cases / 1008 checks' in (target/mode/'simulation.log').read_text()
    assert '4 cases, 318 checks' in (target/'board-sync/simulation.log').read_text()
    spi=list(csv.DictReader((target/'board-sync/metrics.csv').open()))
    ideal=list(csv.DictReader((target/'sync/metrics.csv').open()))
    assert len(spi)==len(ideal)==21
    assert {r['opcode'] for r in spi}=={r['opcode'] for r in ideal}
    assert all(r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177' for r in spi)
    print(json.dumps(dict(passed=True,current_files_checked=current,control_cases=90112,
        hardware='unchanged CP67b',rt11=record['rt11'],installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
