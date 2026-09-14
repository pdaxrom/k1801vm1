#!/usr/bin/env python3
"""Verify frozen CP73 inputs/results/package; optionally compare live sources."""
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
    target=ROOT/'tb/reports/cp73';record=json.loads((target/'archive.json').read_text())
    assert record['passed'] and not record['installed_on_board']
    for name,h in record['artifacts'].items():assert sha(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for name,m in record['source_files'].items():
            assert hashlib.sha256(t.extractfile(m['member']).read()).hexdigest()==m['sha256'],name
            if current:assert sha(ROOT/name)==m['sha256'],name
        vectors=t.extractfile('project/build/cp73-fp11/sync/vectors.txt').read().splitlines()
        marks=Counter();floating_modes=set()
        for case_id,row in enumerate(vectors):
            words=[int(v,16) for v in row.split()];assert len(words)==129
            assert words[79]==case_id  # IDs must not wrap at 16 bits.
            assert all(0<=v<=65535 for i,v in enumerate(words) if i!=79)
            marks[words[128]]+=1
            if words[0]&0o177400 in (0o170400,0o172000,0o172400,0o173000,0o173400,0o174000):
                floating_modes.add((words[0],bool(words[1]&0o200)))
        assert marks==Counter({0:145364,1:384,2:50,3:3166,4:440})
        assert len(floating_modes)==3072  # 1536 encodings in both F/D modes
        assert len(vectors)==149404
    for name,h in record['hardware']['files'].items():assert sha(ROOT/name)==h,name
    release=ROOT/'demos/rt11/service/cp73';manifest=json.loads((release/'release.json').read_text())
    for name,h in manifest['files'].items():assert sha(release/name)==h,name
    assert decode((release/'FP11.BIN').read_bytes())==manifest['format']
    assert manifest['allocation_bytes']==2510 and manifest['supported_encodings']==1685
    assert manifest['rejected_ac_encodings']==48
    expected={'sync':(149404,10426000,4040),'logic':(46040,3254816,3766),'vendor':(472,33088,80)}
    for mode,(cases,checks,manual) in expected.items():
        log=(target/mode/'simulation.log').read_text()
        assert f'{cases} cases / {checks} checks / {manual} manual DEC cases' in log,mode
    for mode in ('events-sync','events-vendor'):
        assert '39 cases / 1389 checks' in (target/mode/'simulation.log').read_text()
    assert '4 cases, 732 checks' in (target/'board-sync/simulation.log').read_text()
    r=json.loads((target/'rt11/result.json').read_text());assert r['passed']
    assert record['rt11']=={n:r[n] for n in ('checks','clocks','uart_bytes')}
    assert f"PASS CP73 RT11 modules: {r['checks']} checks, {r['clocks']} clocks, {r['uart_bytes']} UART bytes" in (target/'rt11/simulation.log').read_text()
    spi=list(csv.DictReader((target/'board-sync/metrics.csv').open()))
    ideal=list(csv.DictReader((target/'sync/metrics.csv').open()))
    assert len(spi)==205
    assert len({r['opcode'] for r in ideal})==1733
    assert {r['opcode'] for r in spi}.issubset({r['opcode'] for r in ideal})
    assert all(r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177' for r in spi)
    performance=json.loads((target/'comparison.json').read_text())
    assert performance['passed'] and performance['operations']==205
    assert performance['unchanged_operations']==63 and len(performance['changed'])==106
    assert len(performance['new_arithmetic'])==36
    assert min(r['delta'] for r in performance['changed'])==384
    assert max(r['delta'] for r in performance['changed'])==576
    math=json.loads((target/'math/result.json').read_text());assert math['passed']
    assert math['comparisons']==200000 and math['exact_error_bounds']==128104
    assert math['guard_differences']
    for mode in ('sync','logic','vendor'):
        adapted=json.loads((target/mode/'adaptation.json').read_text())
        for name,digest in adapted['original'].items():assert record['source_files'][name]['sha256']==digest,name
        for name,digest in adapted['adapted'].items():assert record['source_files'][name]['sha256']==digest,name
    print(json.dumps(dict(passed=True,current_files_checked=current,adapted_reference_cases=145364,marked_cases=4040,math_comparisons=200000,
        hardware='unchanged CP67b',rt11=record['rt11'],installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
