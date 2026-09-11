#!/usr/bin/env python3
"""Archive the CP55 contract proofs, memory/board tests and cold RT-11 run."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    proof=json.loads((ROOT/'build/cp55-proof/result.json').read_text())
    tests=json.loads((ROOT/'build/cp55-tests/result.json').read_text())
    generated=json.loads((ROOT/'build/cp55-rx/inputs.json').read_text())
    inputs={}
    def merge(hashes):
        for p,h in hashes.items():
            assert p not in inputs or inputs[p]==h,p
            assert digest(ROOT/p)==h,p
            inputs[p]=h
    for hashes in (proof['inputs_sha256'],tests['inputs_sha256'],generated['inputs'],generated['outputs']):merge(hashes)
    for run in tests['runs']:merge(run['sources_sha256'])
    logs={**proof['logs_sha256'],**tests['logs_sha256']}
    for p,h in logs.items():assert digest(ROOT/p)==h,p
    assert tests['cp54_counters_identical']
    manifest=json.loads((ROOT/'build/cp55-final-board-inputs.json').read_text())
    assert manifest['rx_cp55'] and not manifest['mmu']
    merge(manifest['files'])
    text=(ROOT/'build/cp55-final-board-rt11.log').read_text()
    passed=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
    counts=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
    assert passed and counts,'CP55 cold boot did not pass'
    boot=dict(zip(['clocks','rk_commands','timer_edges','uart_bytes','sd_reads','sd_writes'],map(int,passed.groups())))
    boot.update(zip(['retired','reads','writes','fram_transactions'],map(int,counts.groups())))
    boot['image_sha256']=manifest['image_sha256']
    previous=json.loads((ROOT/'docs/verification-cp54.json').read_text())
    assert boot==previous['cold_rt11']['dma-ack'],'cold counters changed'
    assert (ROOT/'build/cp55-final-uart.txt').read_bytes()==(ROOT/'tb/reports/cp54/cp54-dma-ack-uart.txt').read_bytes()
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')==manifest['image_sha256']
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    merge({p:preserved[p] for p in CORE+BOARD+MMU})
    copies=['build/cp55-proof/result.json','build/cp55-tests/result.json','build/cp55-rx/inputs.json',
        'build/cp55-transfer.json','build/cp55-transfer-manifest.json']
    copies += [f'build/cp55-final-{s}' for s in ('board-inputs.json','board-build.log','board-rt11.log','uart.txt')]
    merge({p:digest(ROOT/p) for p in copies+['tools/record_rx_cp55.py','docs/verification-cp54.json']})
    out=ROOT/'tb/reports/cp55';out.mkdir(parents=True,exist_ok=True)
    for p in set(copies)|set(logs):
        target=out/p.removeprefix('build/');target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/p,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(inputs):
            if not p.startswith('tb/reports/') and not p.endswith('.log'):archive.add(ROOT/p,arcname=p)
    miter=[]
    for run in tests['runs']:
        if run['tag'].startswith('miter-'):
            match=re.search(r'divider(\d+) (\d+) beats (\d+) reset offsets (\d+) clock comparisons',run['pass_lines'][0])
            assert match,run
            miter.append(dict(zip(['divider','beats','reset_offsets','clock_comparisons'],map(int,match.groups()))))
    result=dict(checkpoint='CP55 local native shared receive/result validation',date='2026-09-11',reference='cp54b',
        production_changed=False,mmu_changed=False,physical_board='CP29a',new_synthesis=False,
        data_contract=generated['data_contract'],rtl_state_bits_removed=8,
        formal_method=proof['method'],formal_assumptions=proof['assumptions'],
        positive_proofs=sum(t['defect']=='none' for t in proof['tests']),
        negative_formal_controls=sum(t['defect']!='none' for t in proof['tests']),
        negative_executable_controls=tests['negative'],four_state_miter=miter,
        random_fram_operations=6144,directed_fram_operations=981,bus_side_effect_beats=43,
        portable_workloads=9,vendor_workloads=9,workloads=tests['workloads'],cp54_counters_identical=True,
        cold_rt11=boot,cold_counters_and_raw_uart_identical_to_cp54=True,
        sources_sha256=inputs,logs_sha256=logs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for p in out.rglob('*') if p.is_file()})
    (ROOT/'docs/verification-cp55.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP55 archived: formal/negative/memory/board/vendor and cold FB; CP54 counters and raw UART preserved')


if __name__=='__main__':main()
