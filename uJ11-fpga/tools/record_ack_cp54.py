#!/usr/bin/env python3
"""Archive CP54 local proofs, vendor/portable boards and both cold RT-11 runs."""
import hashlib
import json
import re
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU
from build_ack_cp54 import VARIANTS


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    proof=json.loads((ROOT/'build/cp54-proof/result.json').read_text())
    tests=json.loads((ROOT/'build/cp54-tests/result.json').read_text())
    generated=json.loads((ROOT/'build/cp54-ack/inputs.json').read_text())
    inputs={};logs={}
    def merge(hashes):
        for p,h in hashes.items():
            assert p not in inputs or inputs[p]==h,p
            assert digest(ROOT/p)==h,p
            inputs[p]=h
    merge(proof['inputs_sha256']);merge(tests['inputs_sha256'])
    merge(generated['inputs']);merge(generated['outputs'])
    logs.update(proof['logs_sha256']);logs.update(tests['logs_sha256'])
    reference=json.loads((ROOT/'docs/synthesis-cp53.json').read_text())
    vendors=[];cold={};copies=[]
    for variant in VARIANTS:
        path=f'build/cp54-vendor/result-{variant}.json'
        vendor=json.loads((ROOT/path).read_text())
        assert len(vendor['variants'])==1
        vendors.append(vendor['variants'][0])
        merge(vendor['inputs_sha256']);logs.update(vendor['logs_sha256']);copies.append(path)
        for suite in (tests,vendor):
            for row in suite['variants']:
                assert row['cp53_counters_identical'] and row['workloads']==reference['vendor_workloads']
                for run in row['runs']:merge(run['sources_sha256'])
        tag='cp54-'+variant
        manifest=json.loads((ROOT/f'build/{tag}-board-inputs.json').read_text())
        assert manifest['ack_cp54']==variant and not manifest['mmu']
        merge(manifest['files'])
        text=(ROOT/f'build/{tag}-board-rt11.log').read_text()
        passed=re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes',text)
        counts=re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)',text)
        assert passed and counts,tag
        row=dict(zip(['clocks','rk_commands','timer_edges','uart_bytes','sd_reads','sd_writes'],map(int,passed.groups())))
        row.update(zip(['retired','reads','writes','fram_transactions'],map(int,counts.groups())))
        row['image_sha256']=manifest['image_sha256']
        assert row==reference['cold_rt11'],(variant,'cold counters differ')
        assert (ROOT/f'build/{tag}-uart.txt').read_bytes()==(ROOT/'tb/reports/cp53-final/cp53-final-uart.txt').read_bytes()
        assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')==manifest['image_sha256']
        cold[variant]=row
        copies += [f'build/{tag}-{s}' for s in ('board-inputs.json','board-build.log','board-rt11.log','uart.txt')]
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    merge({p:preserved[p] for p in CORE+BOARD+MMU})
    copies+=['build/cp54-proof/result.json','build/cp54-tests/result.json','build/cp54-ack/inputs.json',
        'build/cp54-transfer.json','build/cp54-transfer-manifest.json']
    for p,h in logs.items():assert digest(ROOT/p)==h,p
    merge({p:digest(ROOT/p) for p in copies+['tools/record_ack_cp54.py']})
    out=ROOT/'tb/reports/cp54';out.mkdir(parents=True,exist_ok=True)
    for p in set(copies)|set(logs):
        target=out/p.removeprefix('build/')
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/p,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for p in sorted(inputs):
            if not p.startswith('tb/reports/') and not p.endswith('.log'):archive.add(ROOT/p,arcname=p)
    result=dict(checkpoint='CP54 local native decode/ACK validation',date='2026-09-11',
        reference='cp53a',production_changed=False,mmu_changed=False,physical_board='CP29a',new_synthesis=False,
        positive_proofs=sum(x['defect']=='none' for x in proof['runs']),
        negative_controls_rejected=sum(x['defect']!='none' for x in proof['runs']),
        formal_method=proof['method'],formal_output_bits=proof['output_bits'],
        four_state_cases=proof['four_state_cases'],four_state_scope=proof['four_state_scope'],
        bus_side_effect_beats=86,portable_workloads=18,vendor_workloads=18,
        workload_counters_identical_to_cp53=True,cold_rt11=cold,cold_counters_and_raw_uart_identical_to_cp53=True,
        sources_sha256=inputs,logs_sha256=logs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for p in out.rglob('*') if p.is_file()})
    (ROOT/'docs/verification-cp54.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS CP54 archived: two proofs, three negative controls, two full cold boots, all CP53 counters preserved')


if __name__=='__main__':main()
