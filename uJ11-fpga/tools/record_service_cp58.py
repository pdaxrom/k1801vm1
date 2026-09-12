#!/usr/bin/env python3
"""Freeze CP58 simulation evidence; do not claim an unmeasured synthesis result."""
import gzip
import hashlib
import json
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
    assert not (ROOT/'build/cp58a/result.json').exists(), 'Update recorder for an actual synthesis result first'
    dest.mkdir(parents=True,exist_ok=True)
    names=['inputs.json','test-results.json','test-0.log','test-1.log','test-vendor.log',
           'service_cp58_cases.vh','service_cp58_board_cases.vh','fis-inputs.json',
           'board-portable-inputs.json','board-vendor-inputs.json','board-portable.log','board-vendor.log',
           'bench-results.json','bench-portable.log','bench-vendor.log','bench-portable.json','bench-vendor.json']
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
    with tarfile.open(dest/'sources.tgz','w:gz') as ar:
        for path in sorted(paths):ar.add(ROOT/path,arcname=path)
    report=dict(checkpoint='CP58a',reference='CP57e',synthesis='not run; upload awaiting user approval after auto-review rejection',
        prepared_input_revision_sha256=prepared['input_revision_sha256'],microcode_words=1002,
        cpu_scenarios=91,cpu_modes=3,board_scenarios=12,board_modes=2,
        fis_portable=23840,fis_vendor=645,fis_vendor_stride=37,workloads=bench['workloads'],
        cold_clocks=173379163,cold_uart_sha256=sha(dest/'cp58a-cold-uart.txt'),
        identical_guest_counters_and_uart_to_cp56=True,
        service_clocks=dict(entry=258,start=120,step=119),installed=False,
        files={str(p.relative_to(ROOT)):sha(p) for p in sorted(dest.iterdir()) if p.is_file()})
    (ROOT/'docs/verification-cp58.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP58 simulation archive: all tested source hashes verified; synthesis remains unmeasured')


if __name__=='__main__':main()
