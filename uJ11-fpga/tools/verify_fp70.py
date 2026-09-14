#!/usr/bin/env python3
"""Verify frozen CP70 inputs/results/package; optionally compare live sources."""
import argparse
import csv
import hashlib
import json
import tarfile
from collections import Counter
from board_common import ROOT
from module_image_cp67 import decode


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(current=False):
    target=ROOT/'tb/reports/cp70';record=json.loads((target/'archive.json').read_text())
    assert record['passed'] and not record['installed_on_board']
    for name,h in record['artifacts'].items():assert sha(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for name,m in record['source_files'].items():
            assert hashlib.sha256(t.extractfile(m['member']).read()).hexdigest()==m['sha256'],name
            if current:assert sha(ROOT/name)==m['sha256'],name
        vectors=t.extractfile('project/build/cp70-fp11/sync/vectors.txt').read().splitlines()
        marks=Counter();transfer_modes=set()
        for row in vectors:
            words=[int(v,16) for v in row.split()];assert len(words)==129
            marks[words[128]]+=1
            if words[0]&0o177400 in (0o172400,0o174000):
                transfer_modes.add((words[0],bool(words[1]&0o200)))
        assert marks==Counter({0:41676,1:128,2:8})
        assert len(transfer_modes)==1024  # 512 encodings in both F/D modes
    for name,h in record['hardware']['files'].items():assert sha(ROOT/name)==h,name
    release=ROOT/'demos/rt11/service/cp70';manifest=json.loads((release/'release.json').read_text())
    for name,h in manifest['files'].items():assert sha(release/name)==h,name
    assert decode((release/'FP11.BIN').read_bytes())==manifest['format']
    assert manifest['allocation_bytes']==1250 and manifest['supported_encodings']==693
    assert manifest['rejected_ac_encodings']==16
    expected={'sync':(41812,2961696,136),'logic':(12080,862956,136),'vendor':(445,31814,66)}
    for mode,(cases,checks,manual) in expected.items():
        log=(target/mode/'simulation.log').read_text()
        assert f'{cases} cases / {checks} checks / {manual} manual DEC cases' in log,mode
    for mode in ('events-sync','events-vendor'):
        assert '31 cases / 1110 checks' in (target/mode/'simulation.log').read_text()
    assert '4 cases, 450 checks' in (target/'board-sync/simulation.log').read_text()
    r=json.loads((target/'rt11/result.json').read_text());assert r['passed']
    assert record['rt11']=={n:r[n] for n in ('checks','clocks','uart_bytes')}
    assert f"PASS CP70 RT11 modules: {r['checks']} checks, {r['clocks']} clocks, {r['uart_bytes']} UART bytes" in (target/'rt11/simulation.log').read_text()
    spi=list(csv.DictReader((target/'board-sync/metrics.csv').open()))
    ideal=list(csv.DictReader((target/'sync/metrics.csv').open()))
    assert len(spi)==79
    assert len({r['opcode'] for r in ideal})==709
    assert {r['opcode'] for r in spi}.issubset({r['opcode'] for r in ideal})
    assert all(r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177' for r in spi)
    print(json.dumps(dict(passed=True,current_files_checked=current,differential_cases=41676,manual_cases=136,
        hardware='unchanged CP67b',rt11=record['rt11'],installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
