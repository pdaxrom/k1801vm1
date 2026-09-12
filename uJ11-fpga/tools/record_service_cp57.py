#!/usr/bin/env python3
"""Archive verified CP57e fixtures/results and tie them to the measured RTL."""
import gzip
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())


def main():
    out=ROOT/'build/cp57-service'; dest=ROOT/'tb/reports/cp57e';dest.mkdir(parents=True,exist_ok=True)
    gate=read(ROOT/'synth/reports/cp57e/result.json')
    for p,h in gate['inputs']['files'].items():
        if p.startswith('generated:'):continue
        assert sha(ROOT/p)==h,p
    profile=read(out/'inputs.json')
    for p,h in {**profile['inputs'],**profile['outputs']}.items():assert sha(ROOT/p)==h,p
    assert (gate['lut4'],gate['ff'],gate['ebr'],gate['fmax_mhz'])==(1260,342,6,31.982)
    assert gate['fully_routed'] and gate['timing_pass'] and profile['used_words']==1000
    cpu=read(out/'test-results.json');assert cpu['scenarios_per_mode']==44 and cpu['profile']==profile
    for p,h in cpu['files'].items():assert sha(ROOT/p)==h,p
    fis=read(out/'fis-inputs.json')
    for p,h in {**fis['inputs'],**fis['outputs']}.items():assert sha(ROOT/p)==h,p
    assert (fis['portable_cases'],fis['vendor_cases'])==(23840,645)
    for kind in ('portable','vendor'):
        board=read(out/f'board-{kind}-inputs.json')
        for p,h in board['files'].items():assert sha(ROOT/p)==h,p
        assert 'PASS CP57 full board: 10 service scenarios' in (out/f'board-{kind}.log').read_text()
    bench=read(out/'bench-results.json');assert bench['profile_sha256']==sha(out/'inputs.json')
    for run in bench['runs']:
        for p,h in run['sources_sha256'].items():assert sha(ROOT/p)==h,p
    assert len(bench['workloads'])==9
    cold=read(ROOT/'build/cp57e-cold-board-inputs.json')
    for p,h in cold['files'].items():assert sha(ROOT/p)==h,p
    cold_log=(ROOT/'build/cp57e-cold-board-rt11.log').read_text()
    assert 'PASS uJ11 cold RT-11 boot + DIR: 173379163 clocks' in cold_log
    assert (ROOT/'build/cp57e-cold-uart.txt').read_bytes()==(ROOT/'tb/reports/cp56/cp56-final-uart.txt').read_bytes()
    edif=read(out/'edif-audit.json');assert not edif['multiple_drivers'] and not edif['floating']
    assert len(edif['negative_controls'])==3
    names=['test-0.log','test-1.log','test-vendor.log','test-results.json','inputs.json','fis-inputs.json',
        'board-portable.log','board-vendor.log','board-portable-inputs.json','board-vendor-inputs.json',
        'bench-portable.log','bench-vendor.log','bench-portable.json','bench-vendor.json','bench-results.json',
        'edif-audit.json','service_cp57_cases.vh','service_cp57_board_cases.vh']
    for n in names:shutil.copyfile(out/n,dest/n)
    for n in ['cp57-fis-portable.log','cp57-fis-vendor.log','cp57e-cold-board-inputs.json','cp57e-cold-board-rt11.log','cp57e-cold-uart.txt']:
        shutil.copyfile(ROOT/'build'/n,dest/n)
    for n in ['cp57-fis-portable.csv','cp57-fis-vendor.csv']:
        (dest/(n+'.gz')).write_bytes(gzip.compress((ROOT/'build'/n).read_bytes(),mtime=0))
    paths=['tools/run_service_cp57.py','tools/run_service_board_cp57.py','tools/check_service_cp57_fis.py',
        'tools/benchmark_service_cp57.py','tools/record_service_cp57.py','tools/run_fram_cp52.py',
        'tools/run_fis_tests.py','tools/make_fis_vectors.py','tools/fis_reference.py','tools/check_edif_drivers_cp46.py',
        'tb/tb_service_cp57.v','tb/tb_service_board_cp57.v','tb/tb_board_bench_cp51.v','tb/tb_fis.v',
        'tb/test_microasm.py','tb/test_fis.py','build/cp57-service/tb_fis.v','build/cp57-service/core-with-decode.v',
        'build/cp57-service/tb_bench_portable.v','build/cp57-service/tb_bench_vendor.v','build/cp57-service/fis-vendor-vectors.txt']
    with tarfile.open(dest/'fixtures.tgz','w:gz') as tar:
        for p in paths:tar.add(ROOT/p,arcname=p)
    edif_path=ROOT/'build/cp57e/impl1/cp57e_impl1.edi'
    assert sha(edif_path)==edif['edif_sha256']
    (ROOT/'synth/reports/cp57e/design.edi.gz').write_bytes(gzip.compress(edif_path.read_bytes(),mtime=0))
    measurements={}
    for name in ('cp57a','cp57b','cp57c','cp57d','cp57e'):
        r=read(ROOT/f'synth/reports/{name}/result.json')
        measurements[name]={k:r.get(k) for k in ('lut4','ff','ebr','slices','fmax_mhz','microcode_words','fully_routed','timing_pass')}
        measurements[name]['input_revision_sha256']=r['inputs']['input_revision_sha256']
    synthesis=dict(checkpoint='CP57 service bank, full HC1200 board',measurements=measurements,
        selected='cp57e',remaining=dict(lut4=20,slices=5,ebr=1,pio=0,microcode_words=24),
        delta_from_cp56a=dict(lut4=76,ff=3,ebr=0,fmax_mhz=-0.014,microcode_words=46),
        critical_path=dict(delay_ns=31.294,slack_ns=2.561,levels=15,route_percent=61.9),
        installed=False,default_changed=False,external_pin_timing_closed=False)
    (ROOT/'docs/synthesis-cp57.json').write_text(json.dumps(synthesis,indent=2)+'\n')
    timings=[]
    for case,clocks,entry,ret in re.findall(r'board case(\d+) (\d+) clocks; entry(-?\d+) return(-?\d+)',(out/'board-portable.log').read_text()):
        timings.append(dict(case=int(case),clocks=int(clocks),entry_to_first_opcode_request=int(entry),start_to_guest_opcode_request=int(ret)))
    verify=dict(checkpoint='CP57e',input_revision_sha256=gate['inputs']['input_revision_sha256'],
        cpu_scenarios=44,cpu_modes=3,board_scenarios=10,board_modes=2,workloads=bench['workloads'],
        fis_portable=23840,fis_vendor=645,fis_vendor_stride=37,
        prior_cp57d_fis_full_portable_and_vendor=23840,
        cold=dict(clocks=173379163,uart_bytes=3270,uart_sha256=sha(dest/'cp57e-cold-uart.txt'),identical_to_cp56=True),
        service_timing=timings,edif_nets=edif['nets'],edif_negative_controls=3,
        files={str(p.relative_to(ROOT)):sha(p) for p in sorted(dest.iterdir()) if p.is_file()})
    (ROOT/'docs/verification-cp57.json').write_text(json.dumps(verify,indent=2)+'\n')
    print('PASS CP57e record: synthesis inputs, CPU/bus/FIS/bench/cold/EDIF hashes verified')

if __name__=='__main__':main()
