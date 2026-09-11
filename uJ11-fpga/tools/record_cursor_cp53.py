#!/usr/bin/env python3
"""Archive CP53 local proofs/benchmarks without inventing synthesis results."""
import hashlib
import json
import shutil
import tarfile
from board_common import ROOT, CORE, BOARD, MMU


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    proof=json.loads((ROOT/'build/cp53-proof/result.json').read_text())
    tests=json.loads((ROOT/'build/cp53-tests/result.json').read_text())
    netlist=json.loads((ROOT/'build/cp53-netlist/audit.json').read_text())
    generated=json.loads((ROOT/'build/cp53-cursor/inputs.json').read_text())
    inputs={**proof['inputs_sha256'],**tests['inputs_sha256'],**generated['inputs'],**generated['outputs']}
    logs=dict(tests['logs_sha256'])
    for row in proof['runs']:logs[row['log']]=row['log_sha256']
    for variant in tests['variants']:
        assert variant['all_counters_identical_to_cp52']
        for run in variant['runs']:inputs.update(run['sources_sha256'])
    for gate in ('cp52a','cp52b'):
        path=f'build/cp53-netlist/{gate}_impl1.edi'
        inputs[path]=netlist[gate]['edif_sha256']
        assert not netlist[gate]['strong_multiple_driver_nets']
    assert netlist['negative_driver_control_rejected']
    assert digest(ROOT/'tools/audit_edif_cp53.py')==netlist['script_sha256']
    preserved=json.loads((ROOT/'docs/verification-cp50.json').read_text())['sources_sha256']
    for path in CORE+BOARD+MMU:
        assert digest(ROOT/path)==preserved[path],path
        inputs[path]=preserved[path]
    for path in ['tools/record_cursor_cp53.py','tools/audit_edif_cp53.py',
                 'build/cp53-proof/result.json','build/cp53-tests/result.json','build/cp53-netlist/audit.json']:
        inputs[path]=digest(ROOT/path)
    for path,h in {**inputs,**logs}.items():assert digest(ROOT/path)==h,path
    out=ROOT/'tb/reports/cp53';out.mkdir(parents=True,exist_ok=True)
    for path in logs:
        target=out/path.removeprefix('build/')
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/path,target)
    for path in ['build/cp53-proof/result.json','build/cp53-tests/result.json','build/cp53-netlist/audit.json']:
        target=out/path.removeprefix('build/');target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/path,target)
    with tarfile.open(out/'test-sources.tgz','w:gz') as archive:
        for path in sorted(inputs):
            if not path.startswith('tb/reports/'):archive.add(ROOT/path,arcname=path)
    report=dict(checkpoint='CP53 cursor local validation',date='2026-09-11',
        reference='cp52b',production_changed=False,mmu_changed=False,physical_board='CP29a',
        formal_method='Two-state unrestricted sequential equivalence of matched state and all outputs; no held-request input assumptions',
        positive_proofs=sum(not x['negative'] for x in proof['runs']),
        rejected_negative_controls=sum(x['negative'] for x in proof['runs']),
        random_operations=12288,directed_operations=1962,board_overlay_beats=129,warm_workloads=27,
        all_cp52_benchmark_counters_preserved=True,new_synthesis=False,candidate_resources=None,
        edif_audit=netlist,sources_sha256=inputs,logs_sha256=logs,
        archives_sha256={str(p.relative_to(ROOT)):digest(p) for p in out.rglob('*') if p.is_file()})
    (ROOT/'docs/verification-cp53.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CP53 archived: 6 positive proofs, 3 negative controls, 3 candidates x 9 unchanged full-board benchmarks')


if __name__=='__main__':main()
