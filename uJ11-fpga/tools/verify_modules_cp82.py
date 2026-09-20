#!/usr/bin/env python3
"""Verify saved CP82 hardware/RTL evidence without contacting the board."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import tarfile
from board_common import ROOT
from hardware_modules_cp82 import parse_table


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify():
    root=ROOT/'tb/reports/cp82-modules'
    manifest=json.loads((root/'verification.json').read_text())
    for name,digest in manifest['files'].items():assert sha(root/name)==digest,name
    assert manifest['fpga_changed'] is False and manifest['software_fpp']=='CP80'
    phases={}
    for folder in ['01-disable','02-fis-only','03-restore','04-combined']:
        result=json.loads((root/'hardware'/folder/'session.json').read_text())
        raw=(root/'hardware'/folder/'uart.bin').read_bytes()
        assert result['passed'] and result['uart_bytes']==len(raw)
        assert result['driver_sha256']==sha(root/'hardware/hardware_modules_cp82.py')
        assert all(c['complete'] for c in result['commands'])
        for c in result['commands']:
            assert raw[c['offset']:c['end']].decode('ascii','backslashreplace')==c['response']
            assert re.search(c['expected'],c['response'])
        statuses=[c for c in result['commands'] if c['command']=='STATUS']
        assert len(statuses)==2
        assert parse_table(statuses[0]['response'].encode())==result['before']
        assert parse_table(statuses[1]['response'].encode())==result['after']
        if result['phase'] in ('fis-only','combined'):
            boot=result['boot'];assert boot['fresh_banner']
            data=raw[boot['offset']:boot['end']]
            banner=re.search(rb'RT-11FB[^\r\n]*V05\.03',data);assert banner
            assert len(re.findall(rb'\r\n\.',data[banner.end():]))>=3
        phases[result['phase']]=result
    initial=phases['disable']['before'];slot=phases['disable']['slot']
    assert initial[slot]==[0o60000,0o4174,0o142034,0o140407]
    disabled=[r.copy() for r in initial];disabled[slot][3]=0o140401
    pending=[r.copy() for r in initial];pending[slot][3]=0o140403
    assert phases['disable']['after']==disabled
    assert phases['fis-only']['before']==phases['fis-only']['after']==disabled
    assert phases['restore']['before']==disabled and phases['restore']['after']==pending
    assert phases['combined']['before']==phases['combined']['after']==initial
    assert any(c['command']=='OFF'+str(slot) for c in phases['disable']['commands'])
    assert any(c['command']=='FP11' for c in phases['restore']['commands'])
    tested=0;recoveries=0;doubles=0
    for phase,names in [('fis-only',['B81FIS','B81FIJ']),('combined',['B81FIJ','B81FPU','B81FPD'])]:
        commands=phases[phase]['commands']
        starts=[i for i,c in enumerate(commands) if c['command'].startswith('RUN B81') and c['command'] not in ('RUN B81TST','RUN B81DBL')]
        assert [commands[i]['command'] for i in starts]==['RUN '+n for n in names]
        for n,index in zip(names,starts):
            end=next(i for i in range(index+1,len(commands)) if commands[i]['command']=='BYE')
            group=commands[index:end+1]
            tests=[c for c in group if c['command']=='RUN B81TST'];assert len(tests)==1
            response=tests[0]['response'];assert 'CHECKS= 29  ERRORS= 0' in response and 'CP81 PASS' in response
            tested+=1
            double=[c for c in group if c['command']=='RUN B81DBL']
            assert len(double)==int(n=='B81FPD')
            if double:assert 'CP81 DOUBLE PASS' in double[0]['response'];doubles+=1
            expected=[('PRINT 1/0','?DIVISION BY ZERO'),('PRINT SQR(-1)','?NEGATIVE SQUARE ROOT'),
                      ('PRINT 1E30*1E30','?FLOATING OVERFLOW'),('PRINT 2+2','\r\n 4 \r\n')]
            if n in ('B81FIS','B81FIJ'):
                expected += [('PRINT 1E-30*1E-30','?FLOATING UNDERFLOW'),
                             ('PRINT 1/0','?DIVISION BY ZERO'),('PRINT 2+2','\r\n 4 \r\n')]
            actual=[c for c in group if c['command'].startswith('PRINT ')]
            assert len(actual)==len(expected)
            for c,(text,message) in zip(actual,expected):
                assert c['command']==text and message in c['response'] and '\r\nREADY\r\n' in c['response']
                assert '?MON-' not in c['response']
                recoveries+=1
    assert (tested,doubles,recoveries)==(5,1,29)
    assert json.loads((root/'hardware/cleanup.json').read_text())['picocom_restored']
    inputs=json.loads((root/'rtl/inputs.json').read_text())
    result=json.loads((root/'rtl/result.json').read_text());assert result['passed']
    for name,digest in result['files'].items():assert sha(root/'rtl'/name)==digest,name
    with tarfile.open(root/'rtl/source.tgz') as archive:
        for name,digest in inputs['files'].items():
            assert hashlib.sha256(archive.extractfile(name).read()).hexdigest()==digest,name
    assert hashlib.sha256(gzip.decompress((root/'rtl/test.dsk.gz').read_bytes())).hexdigest()==inputs['image_sha256']
    text=(root/'rtl/simulation.log').read_text()
    assert re.findall(r'CP82 RT11 CONFIG=([0-7]+) FPP=([01])',text)==[('124141','1'),('124041','0'),('124141','1')]
    assert 'CP82 B81FIS errors FPP=0 HALT=0' in text
    assert 'CP82 B81FIJ errors FPP=0 HALT=0' in text
    assert re.search(r'CP82 B81FIJ errors FPP=([1-9][0-9]*) HALT=\1\b',text)
    match=re.search(r'PASS CP82 modules: (\d+) checks, (\d+) clocks, (\d+) UART bytes',text);assert match
    assert tuple(map(int,match.groups()))==(result['checks'],result['clocks'],result['uart_bytes'])
    uart=(root/'rtl/uart.txt').read_text()
    assert '?FP11 DEBUG STATE UNAVAILABLE' in uart and 'PSW=' in uart
    print('PASS CP82: physical OFF/cold boot/FIS/restore/FPP, 5 x 29 numerical checks, double and 29 recovery commands')
    print('PASS CP82 RTL: RT11 CONFIG follows FPP, original FIS exceptions, independent ODT, preserved module table')


if __name__=='__main__':verify()
