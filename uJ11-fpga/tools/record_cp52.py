#!/usr/bin/env python3
"""Archive verified CP52 simulation inputs, logs and measurements."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    tests=json.loads((ROOT/'build/cp52-tests/result.json').read_text())
    inputs=dict(tests['inputs_sha256'])
    for run in tests['runs']:
        inputs.update(run['sources_sha256'])
    inputs.update(tests['logs_sha256'])
    cp50=json.loads((ROOT/'docs/verification-cp50.json').read_text())
    for p in CORE+BOARD+MMU:
        assert digest(ROOT/p)==cp50['sources_sha256'][p], 'production changed: '+p
        inputs[p]=digest(ROOT/p)
    boot={}
    logs=[]
    for tag in ('cp52-base','cp52-final'):
        manifest=json.loads((ROOT/f'build/{tag}-board-inputs.json').read_text())
        inputs.update(manifest['files'])
        text=(ROOT/f'build/{tag}-board-rt11.log').read_text()
        match=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
        assert match,tag
        counts=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
        assert counts,tag
        boot[tag]=dict(zip(['clocks','rk_commands','timer_edges','uart_bytes','sd_reads','sd_writes'],map(int,match.groups())))
        boot[tag].update(zip(['retired','reads','writes','fram_transactions'],map(int,counts.groups())))
        boot[tag]['image_sha256']=manifest['image_sha256']
        for suffix in ('board-inputs.json','board-build.log','board-rt11.log','uart.txt'):
            logs.append(f'build/{tag}-{suffix}')
    # The prompt fix must preserve the old native workload to the last clock.
    assert boot['cp52-base']['clocks']==354938300
    assert boot['cp52-base']['image_sha256']==boot['cp52-final']['image_sha256']
    assert boot['cp52-base']['sd_reads']==boot['cp52-final']['sd_reads']==162
    assert boot['cp52-base']['sd_writes']==boot['cp52-final']['sd_writes']==6
    assert boot['cp52-base']['rk_commands']==boot['cp52-final']['rk_commands']==300
    assert boot['cp52-base']['uart_bytes']==boot['cp52-final']['uart_bytes']==3270
    uart_base=(ROOT/'build/cp52-base-uart.txt').read_bytes()
    uart_candidate=(ROOT/'build/cp52-final-uart.txt').read_bytes()
    # SL redraw orders a redundant carriage return differently at the faster
    # CPU speed. Preserve both raw streams; compare text and all other bytes.
    assert uart_base.replace(b'\r',b'')==uart_candidate.replace(b'\r',b'')
    for p in logs+['tools/record_cp52.py','build/cp52-tests/result.json']:
        inputs[p]=digest(ROOT/p)
    for p,h in inputs.items():assert digest(ROOT/p)==h,p
    out=ROOT/'tb/reports/cp52';out.mkdir(parents=True,exist_ok=True)
    for p in logs:
        shutil.copyfile(ROOT/p,out/p.removeprefix('build/'))
    for p in (ROOT/'build/cp52-tests').glob('*.log'):
        shutil.copyfile(p,out/p.name)
    shutil.copyfile(ROOT/'build/cp52-tests/result.json',out/'tests.json')
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(inputs):
            if not p.startswith('tb/reports/') and not p.endswith('.log'):
                archive.add(ROOT/p,arcname=p)
    report=dict(checkpoint='CP52: native demand sequential FRAM READ',date='2026-09-11',
        speculative=False,production_changed=False,physical_board='CP29a',new_synthesis=False,
        resource_baseline=dict(gate='cp40h',lut4=1159,ff=326,ebr=6,fmax_mhz=31.470),
        candidate_resources=None,microcode_words=954,unit_runs=tests['runs'],
        workloads=tests['workloads'],cold_rt11=boot,
        cold_speedup=boot['cp52-base']['clocks']/boot['cp52-final']['clocks'],
        uart_transcripts_identical=uart_base==uart_candidate,
        uart_without_cr_identical=True,sources_sha256=inputs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for p in out.iterdir() if p.is_file()})
    (ROOT/'docs/verification-cp52.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(cold_rt11=boot,cold_speedup=report['cold_speedup'],new_synthesis=False),indent=2))


if __name__=='__main__':main()
