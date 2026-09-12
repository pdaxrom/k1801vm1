#!/usr/bin/env python3
"""Verify selected CP58a synthesis against the frozen simulation evidence."""
import gzip
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())
def verify(files):
    for name,digest in files.items():assert sha(ROOT/name)==digest,name


def main():
    out=ROOT/'build/cp58-service';dest=ROOT/'tb/reports/cp58a'
    profile=read(out/'inputs.json')
    verify(profile['inputs']);verify(profile['outputs'])
    assert profile['used_words']==1002 and profile['reference']=='cp57e'
    cpu=read(out/'test-results.json')
    assert cpu['scenarios_per_mode']==91 and cpu['profile']==profile
    verify(cpu['files'])
    for mode in (0,1,'vendor'):
        assert 'PASS CP58 CPU: 91 scenarios' in (out/f'test-{mode}.log').read_text()
    for kind in ('portable','vendor'):
        verify(read(out/f'board-{kind}-inputs.json')['files'])
        assert 'PASS CP58 full board: 12 service scenarios' in (out/f'board-{kind}.log').read_text()
    fis=read(out/'fis-inputs.json');verify(fis['inputs']);verify(fis['outputs'])
    assert (fis['portable_cases'],fis['vendor_cases'])==(23840,645)
    bench=read(out/'bench-results.json')
    assert bench['profile_sha256']==sha(out/'inputs.json') and len(bench['workloads'])==9
    for run in bench['runs']:verify(run['sources_sha256'])
    cold=read(ROOT/'build/cp58a-cold-board-inputs.json');verify(cold['files'])
    assert 'PASS uJ11 cold RT-11 boot + DIR: 173379163 clocks' in (ROOT/'build/cp58a-cold-board-rt11.log').read_text()
    assert (ROOT/'build/cp58a-cold-uart.txt').read_bytes()==(ROOT/'tb/reports/cp56/cp56-final-uart.txt').read_bytes()
    prepared=read(ROOT/'build/cp58a/inputs.json')
    verify({p:h for p,h in prepared['files'].items() if not p.startswith('generated:')})
    assert sha(ROOT/'build/cp58a/clock.lpf')==prepared['files']['generated:clock.lpf']
    gate=read(ROOT/'synth/reports/cp58a/result.json')
    assert gate['inputs']==prepared
    assert (gate['lut4'],gate['ff'],gate['ebr'],gate['slices'],gate['fmax_mhz'])==(1246,342,6,627,30.743)
    assert gate['fully_routed'] and gate['timing_pass'] and gate['microcode_words']==1002
    edif=read(out/'edif-audit.json')
    assert not edif['multiple_drivers'] and not edif['floating'] and len(edif['negative_controls'])==3
    assert sha(ROOT/'build/cp58a/impl1/cp58a_impl1.edi')==edif['edif_sha256']
    assert hashlib.sha256(gzip.decompress((ROOT/'synth/reports/cp58a/design.edi.gz').read_bytes())).hexdigest()==edif['edif_sha256']
    dest.mkdir(parents=True,exist_ok=True)
    names=['inputs.json','test-results.json','test-0.log','test-1.log','test-vendor.log',
           'service_cp58_cases.vh','service_cp58_board_cases.vh','fis-inputs.json',
           'board-portable-inputs.json','board-vendor-inputs.json','board-portable.log','board-vendor.log',
           'bench-results.json','bench-portable.log','bench-vendor.log','bench-portable.json','bench-vendor.json',
           'edif-audit.json']
    for name in names:shutil.copyfile(out/name,dest/name)
    for name in ['cp58-fis-portable.log','cp58-fis-vendor.log','cp58a-cold-board-inputs.json',
                 'cp58a-cold-board-rt11.log','cp58a-cold-uart.txt']:
        shutil.copyfile(ROOT/'build'/name,dest/name)
    for name in ['cp58-fis-portable.csv','cp58-fis-vendor.csv']:
        (dest/(name+'.gz')).write_bytes(gzip.compress((ROOT/'build'/name).read_bytes(),mtime=0))
    shutil.copyfile(ROOT/'build/cp58a/inputs.json',dest/'prepared-synthesis-inputs.json')
    # Exact tested RTL/ROM and testbench sources, including frozen ancestors.
    paths=set(p for p in prepared['files'] if not p.startswith('generated:'))
    paths.update(p for r in [cpu['files'],fis['inputs'],cold['files']] for p in r)
    paths.update(['tools/record_service_cp58.py','tools/benchmark_service_cp58.py',
                  'tools/run_service_board_cp58.py','tb/tb_service_board_cp58.v',
                  'tools/run_fram_cp52.py','tools/make_fis_vectors.py','tools/fis_reference.py',
                  'tb/tb_board_bench_cp51.v','tb/test_microasm.py','tb/test_fis.py'])
    for run in bench['runs']:paths.update(run['sources_sha256'])
    # Preserve the original simulation archive when adding synthesis evidence.
    if not (dest/'sources.tgz').exists():
        with tarfile.open(dest/'sources.tgz','w:gz') as ar:
            for path in sorted(paths):ar.add(ROOT/path,arcname=path)
    # Verify the archived tested RTL, not just the current generated files.
    with tarfile.open(dest/'sources.tgz') as ar:
        for path,digest in {**profile['inputs'],**profile['outputs']}.items():
            assert hashlib.sha256(ar.extractfile(path).read()).hexdigest()==digest,path
    measurements={}
    for name in ('cp58a','cp58b'):
        measured=read(ROOT/f'synth/reports/{name}/result.json')
        assert measured['fully_routed'] and measured['timing_pass'] and measured['diamond_returncode']==0
        with tarfile.open(ROOT/f'synth/reports/{name}/source.tgz') as ar:
            for path,digest in measured['inputs']['files'].items():
                data=(ROOT/f'synth/reports/{name}/clock.lpf').read_bytes() if path.startswith('generated:') else ar.extractfile(path).read()
                assert hashlib.sha256(data).hexdigest()==digest,(name,path)
        for suffix,digest in measured['reports'].items():
            assert sha(ROOT/f'synth/reports/{name}/design{suffix}')==digest,(name,suffix)
        measurements[name]={k:measured[k] for k in ('lut4','ff','ebr','slices','fmax_mhz','microcode_words','fully_routed','timing_pass')}
        measurements[name]['input_revision_sha256']=measured['inputs']['input_revision_sha256']
    bdest=ROOT/'tb/reports/cp58b'
    bprofile=read(bdest/'inputs.json');bcpu=read(bdest/'test-results.json')
    assert bprofile['used_words']==1001 and bcpu['profile']==bprofile and bcpu['scenarios_per_mode']==91
    for name in ('test-0.log','test-1.log','test-vendor.log','service_cp58_cases.vh'):
        assert sha(bdest/name)==bcpu['files'][f'build/cp58-service/{name}'],name
    for mode in ('portable','vendor'):
        assert 'PASS CP58 full board: 12 service scenarios' in (bdest/f'board-{mode}.log').read_text()
        board=read(bdest/f'board-{mode}-inputs.json')
        assert sha(bdest/'service_cp58_board_cases.vh')==board['files']['build/cp58-service/service_cp58_board_cases.vh']
    bedif=read(bdest/'edif-b-audit.json')
    assert not bedif['multiple_drivers'] and not bedif['floating'] and len(bedif['negative_controls'])==3
    assert hashlib.sha256(gzip.decompress((ROOT/'synth/reports/cp58b/design.edi.gz').read_bytes())).hexdigest()==bedif['edif_sha256']
    with tarfile.open(ROOT/'synth/reports/cp58b/source.tgz') as ar:
        for path,digest in {**bprofile['inputs'],**bprofile['outputs']}.items():
            assert hashlib.sha256(ar.extractfile(path).read()).hexdigest()==digest,path
    twr=(ROOT/'synth/reports/cp58a/design.twr').read_text()
    delay,logic,route,levels=re.search(r'Delay:\s+([\d.]+)ns\s+\(([\d.]+)% logic, ([\d.]+)% route\), (\d+) logic levels',twr).groups()
    synthesis=dict(selected='cp58a',measurements=measurements,
        remaining=dict(lut4=34,slices=13,ebr=1,microcode_words=22,pio=0),
        delta_from_cp57e=dict(lut4=-14,ff=0,ebr=0,fmax_mhz=-1.239,microcode_words=2),
        critical_path=dict(delay_ns=float(delay),logic_percent=float(logic),route_percent=float(route),levels=int(levels),
                           slack_ns=float(re.search(r'meets requirements by ([\d.]+)ns',twr)[1])),
        constraint_mhz=29.56,external_pin_timing_closed=False,osch_tolerance_closed=False,
        installed=False,default_changed=False,
        rejected='cp58b: one fewer microinstruction, but +12 LUT and -0.117 MHz versus cp58a')
    (ROOT/'docs/synthesis-cp58.json').write_text(json.dumps(synthesis,indent=2)+'\n')
    report=dict(checkpoint='CP58a',reference='CP57e',synthesis='full HC1200 MAP/PAR/TRACE PASS at nominal 29.56 MHz',
        prepared_input_revision_sha256=prepared['input_revision_sha256'],microcode_words=1002,
        cpu_scenarios=91,cpu_modes=3,board_scenarios=12,board_modes=2,
        fis_portable=23840,fis_vendor=645,fis_vendor_stride=37,workloads=bench['workloads'],
        cold_clocks=173379163,cold_uart_sha256=sha(dest/'cp58a-cold-uart.txt'),
        identical_guest_counters_and_uart_to_cp56=True,
        service_clocks=dict(entry=258,start=120,step=119),installed=False,
        edif_nets=edif['nets'],edif_negative_controls=3,
        files={str(p.relative_to(ROOT)):sha(p) for directory in [dest,bdest] for p in sorted(directory.iterdir()) if p.is_file()})
    (ROOT/'docs/verification-cp58.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP58a: exact simulation/synthesis source hashes verified; CP58b comparison archived')


if __name__=='__main__':main()
