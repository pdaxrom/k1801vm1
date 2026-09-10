#!/usr/bin/env python3
"""Bind CP34 arithmetic sharing and scratch preservation evidence to sources."""
import json
import re
import shutil
from board_common import ROOT
from record_cp33 import digest, synthesis


def checked_report(filename):
    report = json.loads((ROOT/'build'/filename).read_text())
    for path, expected in report['inputs_sha256'].items():
        assert digest(ROOT/path) == expected, (filename, path, 'test inputs changed')
    return report


def main():
    fits = {name: synthesis(name, name == 'cp34c') for name in ('cp34a', 'cp34b', 'cp34c')}
    production = synthesis('cp31c', True)
    audit = checked_report('cp34-scratch.json')
    dynamic = checked_report('cp34-scratch-dynamic.json')
    coverage = checked_report('cp34-scratch-coverage.json')
    negative = checked_report('cp34-dp-negative.json')
    assert audit['memory_words'] == 88 and audit['dead_at_every_memory_word'] == ['T5', 'T6', 'T7']
    assert audit['negative_controls_pass'] and dynamic['live_t0_negative_detected']
    assert len(dynamic['tests']) == 16 and len(negative['tests']) == 4
    assert len(coverage['tests']) == 5 and len(coverage['aggregate_memory_upcs_covered']) == 88
    scratch_tests = dynamic['tests']+coverage['tests']
    assert all(t['rejected'] for t in negative['tests'])
    stats = json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    assert stats['used_words'] == 954 and stats['encoding_version'] == 12
    logs = ['cp34-sharing.log', 'cp34-relocation.log', 'cp34-lint.log',
            'cp34-scratch-run.log', 'cp34-scratch-coverage-run.log',
            'cp34-negative-run.log', 'cp34-scratch-live-negative.log']
    for candidate, name in [(1, 'cp34-sharing.log'), (2, 'cp34-relocation.log')]:
        log = (ROOT/'build'/name).read_text()
        for full, count in [(0, 265701), (1, 16977381)]:
            assert f'PASS MMU datapath sharing: {count} cycles FULL={full} CANDIDATE={candidate}' in log
        assert '%Error' not in log and '%Warning' not in log and 'FATAL' not in log
    lint = (ROOT/'build/cp34-lint.log').read_text()
    assert 'PASS strict lint: dedicated/shared/relocation' in lint and '%Warning' not in lint and '%Error' not in lint
    for test in scratch_tests:
        logs += [test['tag']+'.log', test['tag']+'-build.log']
        log = (ROOT/'build'/(test['tag']+'.log')).read_text()
        assert all(line in log for line in test['pass_lines'])
        assert f"SCRATCH poison_events={test['poison_events']} live_mutation=0" in log
    for test in negative['tests']:
        tag = 'cp34-negative-'+test['mutation']
        logs += [tag+'.log', tag+'-build.log']
        assert (ROOT/'build'/(tag+'.log')).read_text().strip() == test['failure']
    image = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(image) == xm_sha and image.stat().st_size == 27540480
    out = ROOT/'tb/reports/cp34'
    out.mkdir(parents=True, exist_ok=True)
    for name in logs+['cp34-scratch.json', 'cp34-scratch-dynamic.json',
                      'cp34-scratch-coverage.json', 'cp34-dp-negative.json']:
        shutil.copyfile(ROOT/'build'/name, out/name)
    sources = sorted(set(['Makefile', 'tools/record_cp34.py', 'tools/record_cp33.py',
                          'tb/tb_mmu_dp_compare.v']+
                         list(audit['inputs_sha256'])+list(negative['inputs_sha256'])+
                         [p for report in (dynamic, coverage) for p in report['inputs_sha256']
                          if not p.startswith('build/')]))
    dynamic_case_count = 0
    for test in scratch_tests:
        for line in test['pass_lines']:
            match = re.search(r'(\d+) (?:DCJ11|exact state)', line)
            if match:
                dynamic_case_count += int(match[1])
    report = dict(
        checkpoint='CP34: isolated datapath arithmetic sharing and microcode scratch lifetime',
        date='2026-09-10', synthesis=fits, production_cp31c=production,
        production_inputs_unchanged=True, production_has_mmu=False,
        board_programmed=False, physical_board_revision='CP29a',
        fp11_removed=True, fis_retained=True, microcode_words=954, microcode_free=70,
        measured_probe=dict(includes=['RF16x16', 'Q16', 'ALU', 'MMU relocation/length arithmetic'],
                            harness_ff=157, datapath_ff=16, ebr=0,
                            full_sharing_lut_delta=16, relocation_sharing_lut_delta=-5,
                            adopted_in_production=False),
        tests=dict(full_cycles_per_candidate=16977381, four_state_cycles_per_candidate=265701,
                   candidates=[1, 2], mixed_cycles_per_run=200000,
                   datapath_negative_controls=4, strict_lint=True,
                   scratch_static_memory_words=88, scratch_dead_temporaries=['T5', 'T6', 'T7'],
                   scratch_static_negative_controls=2, scratch_dynamic_suites=len(scratch_tests),
                   scratch_dynamic_cases=dynamic_case_count,
                   scratch_poison_events=sum(t['poison_events'] for t in scratch_tests),
                   scratch_dynamic_memory_words=len(coverage['aggregate_memory_upcs_covered']),
                   scratch_unexercised_memory_words=[],
                   scratch_live_t0_negative_detected=True),
        rt11_xm=dict(path='lsi11/disks/rt11v5.3/system.dsk', sha256=xm_sha,
                     image_modified=False, boot_tested=False),
        sources_sha256={p: digest(ROOT/p) for p in sources},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p)
                                  for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        decision='Reject full arithmetic sharing; retain relocation-only candidate for comparison. '
                 'Explore a microcoded MMU entry/return using dead T5/T6/T7 after preserving other CPU state.',
        limits=['No MMU integration or new CPU microclocks/Fmax benchmark.',
                'Local 5-LUT saving does not prove full-board fit; CP31c has only 28 LUT / 12 slices / 1 EBR free.',
                'Scratch liveness is bound to the current ROM; dynamic fixtures supplement the graph, not exhaustive ISA proof.',
                'No permission to clobber PSW/MDR/IR/Q or the existing CALL link.',
                'MMR, abort/restart, PA22 bus, RK high-memory DMA and RT-11XM remain future gates.'])
    (ROOT/'docs/verification-cp34.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(synthesis=fits, tests=report['tests'], production_unchanged=True, xm_boot_tested=False), indent=2))


if __name__ == '__main__':
    main()
