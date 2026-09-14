#!/usr/bin/env python3
"""Verify CP77 archive, live sources and qualified DEC results without rebuilding."""
import argparse,hashlib,json,tarfile
from pathlib import Path
from board_common import ROOT
from module_image_cp67 import decode

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify(current=False):
    target=ROOT/'tb/reports/cp77';r=json.loads((target/'archive.json').read_text())
    assert r['passed'] and not r['installed_on_board']
    for name,digest in r['artifacts'].items():assert sha(target/name)==digest,name
    with tarfile.open(target/'source.tgz') as t:
        for name,entry in r['source_files'].items():
            assert hashlib.sha256(t.extractfile(entry['member']).read()).hexdigest()==entry['sha256'],name
            if current:assert sha(ROOT/name)==entry['sha256'],name
    for name,ref in r['references'].items():
        with tarfile.open(ROOT/ref['archive']) as t:
            assert hashlib.sha256(t.extractfile(ref['member']).read()).hexdigest()==ref['sha256']
        if current:assert sha(ROOT/name)==ref['sha256'],name
    for name,digest in r['hardware']['files'].items():assert sha(ROOT/name)==digest,name
    release=ROOT/'demos/rt11/service/cp77'
    for name,digest in r['release']['files'].items():assert sha(release/name)==digest,name
    for label,file in (('odt','ODT.BIN'),('fp11','FP11.BIN')):
        assert decode((release/file).read_bytes())==r['release']['modules'][label]['format']
    od=r['release']['modules']['odt'];fp=r['release']['modules']['fp11']
    assert od['format']['base']+od['allocation_bytes']<=fp['format']['base']==0o60000
    assert od['allocation_bytes']==13668 and fp['allocation_bytes']==4560
    counts={'core':187,'breakpoints':112,'fp-debug':9262,'panel':3324,'rt11':99}
    for label,expected in counts.items():
        result=json.loads((target/label/'result.json').read_text());assert result['passed']
        if 'checks' in result:assert result['checks']==expected,label
        else:assert f'{expected} checks' in (target/label/'simulation.log').read_text(),label
    assert '215784 cases / 15052576 checks / 6014 manual' in (target/'logic/simulation.log').read_text()
    for label in ('events-sync','events-vendor'):assert '111 cases / 4174 checks' in (target/label/'simulation.log').read_text()
    for label in ('dec-a','dec-b','dec-c'):
        result=json.loads((target/label/'result.json').read_text());assert result['passed'] and result['psw_read_adapter']
        assert 'TOTAL ERRORS SINCE LAST REPORT      0' in (target/label/'uart.txt').read_text()
    negative=json.loads((target/'dec-native/result.json').read_text());assert not negative['passed'] and not negative['psw_read_adapter']
    assert '004712' in (target/'dec-native/uart.txt').read_text()
    assert 'TOTAL ERRORS SINCE LAST REPORT      1' in (target/'dec-native/uart.txt').read_text()
    for e in r['diagnostics']['entries']:
        assert sha(target/'diagnostics'/e['name'])==e['file_sha256']
        assert sha(target/'diagnostics'/(e['name']+'.ram'))==e['ram_sha256']
    print(json.dumps(dict(passed=True,current_files_checked=current,hardware_inputs=len(r['hardware']['files']),
                         rtl_cases=215784,odt_checks=9262,panel_checks=3324,rt11=r['results']['rt11'],
                         dec='three passes with test-only PSW read; native CP67b PSW compatibility gap reproduced',installed_on_board=False),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--current',action='store_true');verify(p.parse_args().current)
