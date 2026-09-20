#!/usr/bin/env python3
"""Verify archived CP81 BASIC evidence; does not contact or test the board."""
import hashlib
import gzip
import json
from pathlib import Path
import re
import tarfile
from board_common import ROOT


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify():
    root=ROOT/'tb/reports/cp81-basic'
    manifest=json.loads((root/'verification.json').read_text())
    for name,digest in manifest['files'].items():
        assert sha(root/name)==digest,name
    build=json.loads((root/'build/build-inputs.json').read_text())
    assert sha(root/'build/build_basic_cp81.py')==build['builder_sha256']
    for name,digest in build['outputs'].items():assert sha(root/'build'/name)==digest,name
    for name,precision,hardware in build['variants']:
        com=(root/'build'/(name+'.COM')).read_text(errors='replace')
        assert 'BSOT0'+precision+'.'+hardware in com
        assert 'BSOT1'+precision+'.'+hardware in com
        mapping=(root/'build'/(name+'.MAP')).read_text(errors='replace')
        section=re.search(r'Undefined globals:\s*(.*?)\s*Transfer address',mapping,re.S)
        assert section and sorted(section[1].split())==['..MSP$','..NRC$','..UAC$']
    reference=json.loads((root/'simh/results.json').read_text())
    assert len(reference['results'])==4
    for r in reference['results']:
        assert r['pass_'] and 'FAIL' not in r['response']
        if r['program']=='B81TST':assert 'CHECKS= 29  ERRORS= 0' in r['response'] and 'CP81 PASS' in r['response']
        else:assert r['basic']=='B81FPD' and 'CP81 DOUBLE PASS' in r['response']
        raw=(root/'simh'/(r['basic']+'.log')).read_bytes()
        assert r['response'].encode('ascii') in raw
    assert len(reference['recovery'])==12
    for r in reference['recovery']:
        assert r['response'].encode('ascii') in (root/'simh'/(r['basic']+'.log')).read_bytes()
    sim=(root/'rtl/simulation.log').read_text()
    assert 'PASS CP81 BASIC:' in sim
    rows=re.findall(r'CP81 BASIC (B81\w+) clocks=(\d+) FIS=(\d+) FADD=(\d+) FSUB=(\d+) FMUL=(\d+) FDIV=(\d+) FPP=(\d+) HALT=(\d+) PSW=(\d+)',sim)
    assert len(rows)==3
    for r in rows:
        name=r[0];clocks,fis,add,sub,mul,div,fpp,halt,psw=map(int,r[1:])
        assert clocks>0 and psw==0
        if name=='B81FIS':assert fis==add+sub+mul+div and min(add,sub,mul,div)>0 and fpp==halt==0
        else:assert fis==0 and fpp==halt and fpp>0
    inputs=json.loads((root/'rtl/inputs.json').read_text())
    assert hashlib.sha256(gzip.decompress((root/'rtl/test.dsk.gz').read_bytes())).hexdigest()==inputs['image_sha256']
    with tarfile.open(root/'rtl/source.tgz') as archive:
        for name,digest in inputs['files'].items():
            assert hashlib.sha256(archive.extractfile(name).read()).hexdigest()==digest,name
    failed=json.loads((root/'hardware/04-tests/session.json').read_text())
    assert failed['passed'] is False
    failure=(root/'hardware/04-tests/uart.bin').read_bytes()
    assert b'?FLOATING UNDERFLOW' in failure and b'?MON-F-Trap to 4' in failure
    assert json.loads((root/'diagnosis/result.json').read_text())['reproduced']
    adapter=json.loads((root/'adapter/result.json').read_text())
    assert sha(root/'adapter/build_basic_fis_abi_cp81.py')==adapter['builder_sha256']
    for n,h in adapter['outputs'].items():assert sha(root/'adapter'/n)==h,n
    old=(root/'adapter/B81FIJ.unpatched').read_bytes()
    new=(root/'adapter/B81FIJ.SAV').read_bytes();a=adapter['patch']['address']
    assert old[:a]==new[:a] and old[a+2:]==new[a+2:]
    assert int.from_bytes(new[a:a+2],'little')==adapter['symbols']['FIADPT']
    assert sha(root/'adapter/FISABI.MAC')==adapter['source_sha256']
    adapted_ref=json.loads((root/'adapter/simh/results.json').read_text())
    assert len(adapted_ref['recovery'])==7 and adapted_ref['results'][0]['pass_']
    for r in adapted_ref['results']+adapted_ref['recovery']:
        assert r['response'].encode('ascii') in (root/'adapter/simh/B81FIJ.log').read_bytes()
    assert 'PASS CP81 FIS ABI:' in (root/'adapter/rtl/simulation.log').read_text()
    abi_inputs=json.loads((root/'adapter/rtl/inputs.json').read_text())
    with tarfile.open(root/'adapter/rtl/source.tgz') as archive:
        for name,digest in abi_inputs['files'].items():
            assert hashlib.sha256(archive.extractfile(name).read()).hexdigest()==digest,name
    assert hashlib.sha256(gzip.decompress((root/'adapter/rtl/test.dsk.gz').read_bytes())).hexdigest()==abi_inputs['image_sha256']
    sessions=[('05-fpu-tests',2,reference['recovery'][4:])]
    if manifest['adapter_installed']:
        sessions.append(('09-adapter-tests',1,adapted_ref['recovery']))
    for folder,expected_tests,expected_recovery in sessions:
        hardware=json.loads((root/'hardware'/folder/'session.json').read_text())
        assert hardware['passed'] and all(c['complete'] for c in hardware['commands'])
        raw=(root/'hardware'/folder/'uart.bin').read_bytes()
        tests=[c for c in hardware['commands'] if c['command']=='RUN B81TST']
        assert len(tests)==expected_tests
        for c in hardware['commands']:
            assert raw[c['offset']:c['end']].decode('ascii','backslashreplace')==c['response']
            assert re.search(c['expected'],c['response'])
            if c['command']=='STATUS':assert c['response'].count('140407')==3
        for c in tests:assert 'CHECKS= 29  ERRORS= 0' in c['response'] and 'CP81 PASS' in c['response']
        double=[c for c in hardware['commands'] if c['command']=='RUN B81DBL']
        assert len(double)==(1 if folder=='05-fpu-tests' else 0)
        if double:assert 'CP81 DOUBLE PASS' in double[0]['response']
        recovery=[c for c in hardware['commands'] if c['command'].startswith('PRINT ')]
        assert len(recovery)==len(expected_recovery)
        for actual,expected in zip(recovery,expected_recovery):
            assert actual['command']==expected['command']
            assert actual['response'].strip()==expected['response'].strip()
    if manifest['adapter_installed']:
        adapted_back=json.loads((root/'hardware/readback-adapter.json').read_text())
        assert adapted_back['passed'] and adapted_back['bytes']==len(new)==27136
        assert (root/'hardware/share/CFIJ.SAV').read_bytes()==new
        assert sha(root/'hardware/share/CFIJ.SAV')==adapted_back['sha256']
        assert json.loads((root/'hardware/hg-adapter-stopped.json').read_text())['stopped']
        transfer=json.loads((root/'adapter/transfer-manifest.json').read_text())
        helper=root/'adapter/hardware_basic_cp81.py'
        assert sha(helper)==transfer['uJ11-fpga/tools/hardware_basic_cp81.py']['sha256']
        install=json.loads((root/'hardware/08-adapter-install/session.json').read_text())
        assert install['commands']==['LOAD HG','COPY HG:B81FIJ.SAV DK:B81FIJ.SAV',
                                    'COPY DK:B81FIJ.SAV HG:CFIJ.SAV','UNLOAD HG']
        assert (root/'hardware/08-adapter-install/uart.bin').read_bytes().endswith(b'\r\n.')
    readback=json.loads((root/'hardware/readback.json').read_text())
    assert readback['passed'] and len(readback['files'])==5
    for entry in readback['files']:
        name=entry['source'];back=entry['readback']
        src=root/('build' if name.endswith('.SAV') else 'simh')/name
        actual=(root/'hardware/share'/back).read_bytes();expected=src.read_bytes()
        assert actual[:len(expected)]==expected and set(actual[len(expected):])<={0}
        assert hashlib.sha256(actual).hexdigest()==entry['readback_sha256']
    assert json.loads((root/'hardware/hg-stopped.json').read_text())['stopped']
    assert manifest['fpga_changed'] is False and manifest['software_fpp']=='CP80'
    print('PASS CP81 archive: original BASIC builds, FIS adapter SIMH/RTL, FPU single/double physical UART; original FIS failure retained')
    print('FIS adapter installation: '+('PASS with SD readback and UART' if manifest['adapter_installed'] else 'PENDING; not claimed as hardware PASS'))


if __name__=='__main__':verify()
