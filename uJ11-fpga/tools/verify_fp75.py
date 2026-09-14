#!/usr/bin/env python3
"""Verify frozen CP75 inputs/results/package; optionally compare live sources."""
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
    target=ROOT/'tb/reports/cp75';record=json.loads((target/'archive.json').read_text())
    assert record['passed'] and not record['installed_on_board']
    for name,h in record['artifacts'].items():assert sha(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for name,m in record['source_files'].items():
            assert hashlib.sha256(t.extractfile(m['member']).read()).hexdigest()==m['sha256'],name
            if current:assert sha(ROOT/name)==m['sha256'],name
        def frozen(name):
            return t.extractfile(record['source_files'][name]['member']).read().decode()
        def body(source,name):
            # Prototype is followed by ';'; match the actual definition.
            import re
            match=re.search(r'^static [^\n]+ '+name+r'\([^;{]+\)\n\{',source,re.M)
            assert match,name
            return source[match.start():source.index('\n}\n',match.end())+3]
        original=frozen('../core/pdp11_fp.c')
        for mode in ('sync','logic','vendor'):
            adapted=frozen('build/cp75-fp11/'+mode+'/reference/pdp11_fp.c')
            for name in ('modfp11','frac_mulfp11','round_and_pack'):
                assert body(original,name)==body(adapted,name),(mode,name)
        vectors=t.extractfile('project/build/cp75-fp11/sync/vectors.txt').read().splitlines()
        marks=Counter();floating_modes=set()
        for case_id,row in enumerate(vectors):
            words=[int(v,16) for v in row.split()];assert len(words)==129
            assert words[79]==case_id  # IDs must not wrap at 16 bits.
            assert all(0<=v<=65535 for i,v in enumerate(words) if i!=79)
            marks[words[128]]+=1
            if words[0]&0o177400 in (0o170400,0o171000,0o171400,0o174400,0o172000,0o172400,0o173000,0o173400,0o174000):
                floating_modes.add((words[0],bool(words[1]&0o200)))
        assert marks==Counter({0:220828,1:576,2:74,3:3166,4:440})
        assert len(floating_modes)==4608  # 2304 encodings in both F/D modes
        assert len(vectors)==225084
    for name,h in record['hardware']['files'].items():assert sha(ROOT/name)==h,name
    release=ROOT/'demos/rt11/service/cp75';manifest=json.loads((release/'release.json').read_text())
    for name,h in manifest['files'].items():assert sha(release/name)==h,name
    assert decode((release/'FP11.BIN').read_bytes())==manifest['format']
    assert (release/'FP11.BIN').stat().st_size==4096
    assert manifest['format']['bytes']==3154 and manifest['format']['words']==1577
    assert manifest['allocation_bytes']==3420 and manifest['supported_encodings']==2429
    assert manifest['rejected_ac_encodings']==72
    expected={'sync': (225084, 15632368, 4256), 'logic': (81544, 5707456, 3982), 'vendor': (276, 19712, 68)}
    for mode,(cases,checks,manual) in expected.items():
        log=(target/mode/'simulation.log').read_text()
        assert f'{cases} cases / {checks} checks / {manual} manual DEC cases' in log,mode
    for mode in ('events-sync','events-vendor'):
        assert '49 cases / 1742 checks' in (target/mode/'simulation.log').read_text()
    assert '4 cases, 848 checks' in (target/'board-sync/simulation.log').read_text()
    r=json.loads((target/'rt11/result.json').read_text());assert r['passed']
    assert record['rt11']=={n:r[n] for n in ('checks','clocks','uart_bytes')}
    assert f"PASS CP75 RT11 modules: {r['checks']} checks, {r['clocks']} clocks, {r['uart_bytes']} UART bytes" in (target/'rt11/simulation.log').read_text()
    spi=list(csv.DictReader((target/'board-sync/metrics.csv').open()))
    ideal=list(csv.DictReader((target/'sync/metrics.csv').open()))
    assert len(spi)==259
    assert len({r['opcode'] for r in ideal})==2501
    assert {r['opcode'] for r in spi}.issubset({r['opcode'] for r in ideal})
    assert all(r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177' for r in spi)
    performance=json.loads((target/'comparison.json').read_text())
    assert performance['passed'] and performance['operations']==259
    assert performance['unchanged_operations']==133 and len(performance['changed'])==108
    assert len(performance['new_arithmetic'])==18
    assert Counter((r['delta'],r['beat_delta']) for r in performance['changed'])==Counter({(192,3):72,(322,5):18,(72,1):18})
    math=json.loads((target/'math/result.json').read_text());assert math['passed']
    assert math['comparisons']==200000 and math['original_mod_comparisons']==200000
    assert math['quantized_product_cases']==99596 and math['both_parts_nonzero']==104660
    baseline=json.loads((target/'baseline.json').read_text())
    assert baseline['passed'] and baseline['unchanged_previous_cases']==194396 and baseline['new_cases']==30688
    for mode in ('sync','logic','vendor'):
        adapted=json.loads((target/mode/'adaptation.json').read_text())
        for name,digest in adapted['original'].items():assert record['source_files'][name]['sha256']==digest,name
        for name,digest in adapted['adapted'].items():assert record['source_files'][name]['sha256']==digest,name
    print(json.dumps(dict(passed=True,current_files_checked=current,adapted_reference_cases=220828,marked_cases=4256,math_comparisons=200000,
        hardware='unchanged CP67b',rt11=record['rt11'],installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
