#!/usr/bin/env python3
"""Verify frozen CP76 inputs/results/package; optionally compare live sources."""
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
    target=ROOT/'tb/reports/cp76';record=json.loads((target/'archive.json').read_text())
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
            adapted=frozen('build/cp76-fp11/'+mode+'/reference/pdp11_fp.c')
            for name in ('modfp11','frac_mulfp11','round_and_pack'):
                assert body(original,name)==body(adapted,name),(mode,name)
        vectors=t.extractfile('project/build/cp76-fp11/sync/vectors.txt').read().splitlines()
        marks=Counter();floating_modes=set()
        for case_id,row in enumerate(vectors):
            words=[int(v,16) for v in row.split()];assert len(words)==129
            assert words[79]==case_id  # IDs must not wrap at 16 bits.
            assert all(0<=v<=65535 for i,v in enumerate(words) if i!=79)
            marks[words[128]]+=1
            if words[0]&0o177400 in (0o170400,0o171000,0o171400,0o174400,0o172000,0o172400,0o173000,0o173400,0o174000,0o176000,0o177400):
                floating_modes.add((words[0],bool(words[1]&0o200)))
        assert marks==Counter({0:436396,1:704,2:90,3:3166,4:440,5:3648})
        assert len(floating_modes)==5632  # 2816 floating encodings in both F/D modes
        assert len(vectors)==444444
        vendor=json.loads((target/'vendor/result.json').read_text())
        if vendor.get('partitions'):
            combined=[];offset=0;total_checks=total_manual=0
            for part in vendor['partitions']:
                assert part['start']==offset and offset%8==0
                rows=(target/'vendor'/part['folder']/'vectors.txt').read_bytes().splitlines()
                assert len(rows)==part['cases'];offset+=len(rows);combined.extend(rows)
                checks=sum(68+2*int(row.split()[60],16) for row in rows)
                manual=sum(bool(int(row.split()[128],16)) for row in rows)
                assert checks==part['checks'] and manual==part['manual']
                assert f"{len(rows)} cases / {checks} checks / {manual} manual DEC cases" in (target/'vendor'/part['folder']/'simulation.log').read_text()
                total_checks+=checks;total_manual+=manual
            assert combined==t.extractfile('project/build/cp76-fp11/vendor/vectors.txt').read().splitlines()
            assert offset==1092 and total_checks==76360 and total_manual==80
    for name,h in record['hardware']['files'].items():assert sha(ROOT/name)==h,name
    release=ROOT/'demos/rt11/service/cp76';manifest=json.loads((release/'release.json').read_text())
    for name,h in manifest['files'].items():assert sha(release/name)==h,name
    assert decode((release/'FP11.BIN').read_bytes())==manifest['format']
    assert (release/'FP11.BIN').stat().st_size==5120
    assert manifest['format']['bytes']==4282 and manifest['format']['words']==2141
    assert manifest['allocation_bytes']==4550 and manifest['supported_encodings']==3949
    assert manifest['rejected_ac_encodings']==88
    expected={'sync': (444444, 30951840, 8048), 'logic': (215784, 15052576, 6014), 'vendor': (1092, 76360, 80)}
    for mode,(cases,checks,manual) in expected.items():
        log=(target/mode/'simulation.log').read_text()
        assert f'{cases} cases / {checks} checks / {manual} manual DEC cases' in log,mode
    for mode in ('events-sync','events-vendor'):
        assert '111 cases / 4174 checks' in (target/mode/'simulation.log').read_text()
    assert '5 cases, 1144 checks' in (target/'board-sync/simulation.log').read_text()
    r=json.loads((target/'rt11/result.json').read_text());assert r['passed']
    assert record['rt11']=={n:r[n] for n in ('checks','clocks','uart_bytes')}
    assert f"PASS CP76 RT11 modules: {r['checks']} checks, {r['clocks']} clocks, {r['uart_bytes']} UART bytes" in (target/'rt11/simulation.log').read_text()
    spi=list(csv.DictReader((target/'board-sync/metrics.csv').open()))
    ideal=list(csv.DictReader((target/'sync/metrics.csv').open()))
    assert len(spi)==403
    assert len({r['opcode'] for r in ideal})==4037
    assert {r['opcode'] for r in spi}.issubset({r['opcode'] for r in ideal})
    assert all(r['entry_to_handler_clocks']=='316' and r['start_fetch_to_return_clocks']=='177' for r in spi)
    performance=json.loads((target/'comparison.json').read_text())
    assert performance['passed'] and performance['operations']==403
    assert performance['unchanged_operations']==5 and len(performance['changed'])==254
    assert len(performance['new_conversions'])==144
    assert Counter((r['delta'],r['beat_delta']) for r in performance['changed'])==Counter({(384,6):254})
    math=json.loads((target/'math/result.json').read_text());assert math['passed']
    assert math['comparisons']==251328 and math['per_family']==[20256,20000,20000,85536,85536,20000]
    assert math['exhaustive_exponents']==65536 and math['exhaustive_short_integers']==65536
    baseline=json.loads((target/'baseline.json').read_text())
    assert baseline['passed'] and baseline['unchanged_previous_cases']==223324 and baseline['corrected_ldf_fiuv_cases']==1760 and baseline['new_cases']==219360
    for mode in ('sync','logic','vendor'):
        adapted=json.loads((target/mode/'adaptation.json').read_text())
        for name,digest in adapted['original'].items():assert record['source_files'][name]['sha256']==digest,name
        for name,digest in adapted['adapted'].items():assert record['source_files'][name]['sha256']==digest,name
    print(json.dumps(dict(passed=True,current_files_checked=current,adapted_reference_cases=436396,marked_cases=8048,math_comparisons=251328,
        hardware='unchanged CP67b',rt11=record['rt11'],installed_on_board=False),indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
