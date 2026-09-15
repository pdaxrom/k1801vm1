#!/usr/bin/env python3
"""Check CP78 functional evidence; this is not a synthesis/board qualification."""
import argparse,hashlib,json,tarfile
from board_common import ROOT

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify(current=False):
    target=ROOT/'tb/reports/cp78';r=json.loads((target/'archive.json').read_text())
    assert r['functional_pass'] and not r['synthesis_completed'] and not r['installed_on_board']
    for name,h in r['artifacts'].items():assert sha(target/name)==h,name
    with tarfile.open(target/'source.tgz') as t:
        for p,e in r['source_files'].items():
            assert hashlib.sha256(t.extractfile(e['member']).read()).hexdigest()==e['sha256'],p
            if current:assert sha(ROOT/p)==e['sha256'],p
    for label in [f'psw-d{d}-a{a}' for d in (0,1) for a in (0,1)]+['psw-vendor']:
        v=r['results'][label];assert v['passed'] and v['cases']==1741 and v['checks']==12486,label
        assert v['clocks']==(228164 if v['decode']==0 else 238577),label
    for label in ('events-sync','events-vendor'):
        assert r['results'][label]['passed']
        assert '111 cases / 4174 checks' in (target/label/'simulation.log').read_text()
    for label,clocks in (('dec-a',177182640),('dec-b',2669489),('dec-c',1476999)):
        v=r['results'][label];assert v['passed'] and not v['psw_read_adapter'] and v['hardware']=='cp78'
        assert v['clocks']==clocks
        assert 'TOTAL ERRORS SINCE LAST REPORT      0' in (target/label/'uart.txt').read_text()
    assert r['results']['rt11']['passed'] and r['results']['rt11']['checks']==99
    for e in r['diagnostics']['entries']:
        assert sha(target/'diagnostics'/e['name'])==e['file_sha256']
        assert sha(target/'diagnostics'/(e['name']+'.ram'))==e['ram_sha256']
    assert r['microcode_words']==1005 and r['expected_ebr']==7
    assert sorted(r['hardware_changes'])==['src/rtl/uj11_core.v','src/rtl/uj11_engine.v','src/rtl/uj11_psw.v']
    print(json.dumps(dict(functional_pass=True,current_files_checked=current,
        psw_cases=1741,psw_checks=12486,psw_configurations=5,dec='three original diagnostics, zero errors, no PSW adapter',
        rt11=r['results']['rt11'],synthesis_completed=False,installed_on_board=False),indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--current',action='store_true');verify(p.parse_args().current)
