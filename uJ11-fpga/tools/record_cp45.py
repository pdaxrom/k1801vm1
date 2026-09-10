#!/usr/bin/env python3
"""Bind CP45 physical-bus area savings to exact synthesis and regression inputs."""
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked, failed_fit


def main():
    fits = {f'cp45{x}': failed_fit(f'cp45{x}') for x in 'abcdefghijk'}
    expected = [(1339,360,672), (1365,359,686), (1362,359,683), (1320,359,662),
                (1350,359,677), (1372,360,688), (1302,359,652), (1357,359,680),
                (1330,359,666), (1338,359,670), (1302,359,652)]
    assert [(r['lut4'], r['ff'], r['slices']) for r in fits.values()] == expected
    assert all(r['ebr'] == 7 for r in fits.values())
    baseline = failed_fit('cp44e'); production = synthesis('cp40h'); accepted_apr = synthesis('cp43d')
    inputs = json.loads((ROOT/'synth/reports/cp45k/inputs.json').read_text())['files']
    sources = {p for p in inputs if not p.startswith('generated:')}
    for p in sources: assert digest(ROOT/p) == inputs[p], (p, 'final fit input changed')
    for name in ('cp40h', 'cp43d', 'cp44e'):
        old = json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for p, h in old.items():
            if not p.startswith('generated:') and p.endswith(('.v','.mem','.lpf','.sty')):
                assert digest(ROOT/p) == h, (name, p, 'baseline hardware changed')
    build = checked('cp45-bus/inputs.json')
    for p, h in build['outputs_sha256'].items(): assert digest(ROOT/p) == h, p
    sources.update(build['inputs_sha256']); sources.update(build['outputs_sha256'])
    proof = checked('cp45-proof.json'); sources.update(proof['inputs_sha256'])
    sources.update(['tools/record_cp45.py', 'tools/record_cp33.py', 'tools/record_cp36.py',
                    'tools/record_cp37.py', 'tools/archive_synthesis.py'])
    assert len(proof['tests']) == 12 and len(proof['four_state']) == 10
    logs = ['cp45-bus/inputs.json', 'cp45-proof.json']
    for test in proof['tests']:
        name = test['tag']+'.log'; text = (ROOT/'build'/name).read_text(); logs.append(name)
        if test['defect'] == 'none':
            assert test['returncode'] == 0 and '81 are proven and 0 are unproven' in text
        else: assert test['returncode'] != 0 and 'unproven' in text
    for test in proof['four_state']:
        tag = test['tag']; logs.extend([tag+'.log', tag+'-build.log'])
        assert not (ROOT/f'build/{tag}-build.log').read_text()
        text = (ROOT/f'build/{tag}.log').read_text()
        if test['variant'] in ('parallel', 'paired'):
            assert test['returncode'] != 0 and 'four-state mismatch' in text and 'zzzz' in text
        else: assert test['returncode'] == 0 and '131072 cases' in text
    prefix = 'cp45-narrow-rom'; tests = {}
    for suite in ('direct-cpu-portable-4096', 'direct-cpu-vendor-4',
                  'direct-cpu-portable-edges', 'direct-cpu-vendor-edges', 'bus'):
        tag = prefix+'-'+suite; result = checked(tag+'.json'); sources.update(result['inputs_sha256'])
        assert result['cp45_variant'] == 'narrow-rom'
        text = (ROOT/f'build/{tag}.log').read_text()
        assert 'FATAL' not in text and all(s in text for s in result['pass_lines'])
        prior = json.loads((ROOT/f'tb/reports/cp44/cp44-{suite}.json').read_text())
        assert result['pass_lines'] == prior['pass_lines'], suite
        tests[suite] = result['pass_lines']; logs.extend(tag+s for s in ('.json','.log','-build.log'))
    board = checked(prefix+'-board-verified.json', 'files'); sources.update(board['files'])
    assert board['cp45_variant'] == 'narrow-rom' and board['cp45_suite'] == 'board'
    text = (ROOT/f'build/{prefix}-board-rt11.log').read_text()
    before = (ROOT/'tb/reports/cp44/cp44-board-rt11.log').read_text()
    counts = board_counts(text); assert counts == board_counts(before)
    mmu = re.search(r'^MMU COUNTS .*$', text, re.M)[0]
    assert mmu == re.search(r'^MMU COUNTS .*$', before, re.M)[0]
    assert digest(ROOT/f'build/{prefix}-uart.txt') == digest(ROOT/'tb/reports/cp44/cp44-uart.txt')
    assert board['image_sha256'] == digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend(prefix+s for s in ('-board-verified.json', '-board-inputs.json', '-board-build.log', '-board-rt11.log', '-uart.txt'))
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm) == xm_sha and xm.stat().st_size == 27540480
    # Compile-time legacy reference copies are derived only by WIDTH scope.
    for folder in (prefix+'-test-reference', prefix+'-bus'):
        sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build'/folder).glob('*.v'))
    out = ROOT/'tb/reports/cp45'; out.mkdir(parents=True, exist_ok=True)
    for path in logs:
        dest = out/path; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/'build'/path, dest)
    with tarfile.open(out/'test-sources.tgz', 'w:gz') as archive:
        for p in sorted(sources):
            if p.startswith(('/', 'build/vendor/', 'synth/reports/', 'tb/reports/')): continue
            name = 'reference-repository/'+p[3:] if p.startswith('../') else p
            archive.add(ROOT/p, arcname=name)
    report = dict(checkpoint='CP45: exact physical bus decode/read mux, partial relocation still over HC1200 capacity',
        date='2026-09-10', synthesis=fits, baseline_cp44e=baseline, best_area_candidate='cp45k',
        candidate_variant='narrow-rom', adopted=False, production_cp40h=production, accepted_apr_cp43d=accepted_apr,
        final_fit_inputs_match_current=True, baseline_hardware_unchanged=True,
        delta=dict(lut4=-49, ff=0, ebr=0, slices=-24, clocks=0),
        over_capacity=dict(lut4=22, slices=12), spare_ebr=0, fmax_mhz=None,
        microcode_words=954, native_cpu_and_relocation_unchanged=True,
        binary_equivalence=dict(variants=10, observable_bits=50, total_equivalence_points=81,
                                unproven=0, detected_alias_mutations=2),
        four_state=dict(passing_variants=8, cases_per_variant=131072,
                        rejected_variants=['parallel', 'paired'], reason='Selected Z memory word becomes X'),
        tests=tests, cold_rt11fb=counts, cold_rt11fb_mmu_line=mmu, cold_counts_and_uart_match_cp44=True,
        fb_image_sha256=board['image_sha256'], fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False, image_modified=False, sha256=xm_sha),
        physical_board='CP29a', board_programmed=False,
        sources_sha256={p: digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['Every CP45 full-board MAP fails capacity; no PAR/TRACE or Fmax is available.',
                'Selected bus changes only combinational decode/local ROM/read mux; ACK, state updates and peripherals are unchanged.',
                'Four-state data tests use known address/control/state inputs; they do not model metastability or analog glitches.',
                'Only CP44 kernel unified PAR relocation: no PDR protection/W, MMR1/2, hardware fault/page metadata or abort/restart.',
                'Processor modes/I-D/CSM/MAP/high DMA are absent; cold FB has zero MMU-enabled private ROM/DMA beats.',
                'RT-11XM and active-MMU RK transfer remain untested. Production/accepted APR/FPGA are unchanged.'])
    (ROOT/'docs/verification-cp45.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS CP45 evidence: eleven exact-source failed fits, -49 LUT; binary/four-state/CPU/bus/cold FB, baseline unchanged')


if __name__ == '__main__': main()
