#!/usr/bin/env python3
"""Bind CP37 lookup-only measurements to exact sources and passing regressions."""
import hashlib
import json
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts


def checked(name, key='inputs_sha256'):
    result = json.loads((ROOT/'build'/name).read_text())
    for path, expected in result[key].items():
        assert digest(ROOT/path) == expected, (name, path, 'test input changed')
    return result


def failed_fit(name):
    folder = ROOT/'synth/reports'/name
    result = json.loads((folder/'result.json').read_text())
    assert result['diamond_returncode'] != 0 and not result['fully_routed']
    assert not result['timing_pass'] and 'fmax_mhz' not in result
    assert result['lut4'] > 1280 and result['slices'] > 640
    assert json.loads((folder/'inputs.json').read_text()) == result['inputs']
    with tarfile.open(folder/'source.tgz') as archive:
        for path, expected in result['inputs']['files'].items():
            data = (folder/'clock.lpf').read_bytes() if path.startswith('generated:') else archive.extractfile(path).read()
            assert hashlib.sha256(data).hexdigest() == expected, (name, path)
    for suffix, expected in result['reports'].items():
        assert digest(folder/('design'+suffix)) == expected
    return {k: result[k] for k in ('lut4', 'ff', 'ebr', 'slices', 'timing_pass', 'fully_routed')}


def main():
    fits = {name: failed_fit(name) for name in ('cp37a', 'cp37b', 'cp37c')}
    fits['cp37d'] = synthesis('cp37d')
    fits['cp37e'] = synthesis('cp37e', True)
    assert fits['cp37e'] == fits['cp37d']
    assert digest(ROOT/'build/cp37e/uj11_board.v') == digest(ROOT/'build/cp37-board/uj11_board.v')
    prior = json.loads((ROOT/'synth/reports/cp37d/inputs.json').read_text())['files']
    final = json.loads((ROOT/'synth/reports/cp37e/inputs.json').read_text())['files']
    for p, expected in prior.items():
        if p.endswith(('.v', '.mem', '.lpf', '.sty')):
            assert final[p.replace('build/cp37d/', 'build/cp37e/')] == expected, p
    production = synthesis('cp36f', True)
    context = synthesis('cp36g')
    build = checked('cp37-lookup/inputs.json')
    for p, expected in build['outputs_sha256'].items():
        assert digest(ROOT/p) == expected
    assert build['retained_address'] == 'page3' and build['scheduled_read'] and build['masked_d']
    assert build['microcode_words'] == 963 and build['original_words_unchanged'] == 954
    assert build['overhead_without_external_hold'] == 10
    cpu = [checked('cp37-miter-'+tag+'.json') for tag in
           ('verilator', 'iverilog', 'iverilog-no-holds')]
    assert [r['cases'] for r in cpu] == [69632, 1024, 1024]
    assert len(cpu[0]['memory_upcs']) == 88
    assert any('page mask ff' in line for line in cpu[0]['pass_lines'])
    assert any('0 held edges' in line for line in cpu[2]['pass_lines'])
    negative = checked('cp37-negative.json')
    assert len(negative['tests']) == 6 and negative['assembler_rejections'] == 8
    lint = checked('cp37-lint.json')
    lint_text = (ROOT/'build/cp37-lint.log').read_text()
    assert 'PASS strict CP37 lint' in lint_text and '%Warning' not in lint_text and '%Error' not in lint_text
    fis = checked('cp37-fis.json')
    assert [t['cases'] for t in fis['tests']] == [23840, 23840, 645]
    for p, expected in fis['vendor_sha256'].items():
        assert digest(ROOT/'build/vendor'/p) == expected
    board = checked('cp37-entry-board.json', 'files')
    counts = board_counts((ROOT/'build/cp37-entry-board-rt11.log').read_text())
    assert counts['sd_reads'] == 162 and counts['sd_writes'] == 6 and counts['rk_commands'] == 300
    assert digest(ROOT/'build/cp37-entry-uart.txt') == digest(ROOT/'tb/reports/cp36/cp36-uart.txt')
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk') == board['image_sha256']
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm) == xm_sha and xm.stat().st_size == 27540480
    logs = ['cp37-negative.json', 'cp37-lint.json', 'cp37-lint.log', 'cp37-fis.json',
            'cp37-entry-board.json', 'cp37-entry-board-inputs.json', 'cp37-entry-board-build.log',
            'cp37-entry-board-rt11.log', 'cp37-entry-uart.txt']
    for r in cpu:
        tag = 'cp37-miter-'+r['simulator']+('' if r['external_holds'] else '-no-holds')
        logs += [tag+'.json', tag+'.log', tag+'-build.log']
        assert all(line in (ROOT/'build'/(tag+'.log')).read_text() for line in r['pass_lines'])
    for test in fis['tests']:
        tag = test['tag']; logs += [tag+'.log', tag+'-build.log']
        assert test['pass_line'] in (ROOT/'build'/(tag+'.log')).read_text()
    for test in negative['tests']:
        tag = 'cp37-negative-'+test['mutation']; logs += [tag+'.log', tag+'-build.log']
        assert test['failure'] == (ROOT/'build'/(tag+'.log')).read_text().strip()
    out = ROOT/'tb/reports/cp37'; out.mkdir(parents=True, exist_ok=True)
    for name in logs:
        shutil.copyfile(ROOT/'build'/name, out/name)
    sources = {'Makefile', 'tools/record_cp37.py', 'tools/checkpoint_apr_lookup.py'}
    for r in [build, negative, lint, fis]+cpu:
        sources.update(r['inputs_sha256'])
    sources.update(build['outputs_sha256'])
    sources.update(board['files'])
    sources = {p for p in sources if (ROOT/p).is_file()}
    # Include generated fixture/reference copies so a clean checkout can audit
    # every positive/negative input independently of future generator changes.
    with tarfile.open(out/'test-sources.tgz', 'w:gz') as archive:
        for p in sorted(sources):
            archive.add(ROOT/p, arcname=p)
    report = dict(checkpoint='CP37: kernel unified read-only APR lookup into T6/T7',
        date='2026-09-10', synthesis=fits, production_cp36f=production,
        context_cp36g=context, selected_experiment='cp37e', production_unchanged=True,
        physical_board='CP29a', board_programmed=False, production_microcode_words=954,
        experimental_microcode_words=963, helper_words=9, overhead_clocks_per_memory_word=10,
        full_board_cost_delta_from_context=dict(lut4=22, ff=3, ebr=1, slices=9),
        remaining_experiment_resources=dict(lut4=15, slices=5, ebr=0),
        cpu_pass_lines=[r['pass_lines'] for r in cpu], covered_memory_words=88,
        reset_positions=9, detected_rtl_microcode_mutations=6, assembler_rejections=8,
        fis=fis['tests'], rt11fb_board=counts, rt11fb_uart_unchanged=True,
        rt11_xm=dict(path='lsi11/disks/rt11v5.3/system.dsk', sha256=xm_sha,
                     boot_tested=False, image_modified=False),
        sources_sha256={p: digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p)
                                  for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['Read-only cost floor: EBR writes tied off; CPU CSR/write arbitration is excluded.',
                'Only kernel unified page selection; no CM/PM/RS banks or I/D selection.',
                'No translation, PDR checks/W updates, MMR, abort/restart, PA22 CPU bus or high DMA.',
                'Nonzero APR fixtures loaded only by the portable testbench, not CPU software.',
                'FIS/vendor and FB tests use zero-initialized APR; full MMU/XM not tested.',
                '15 LUT/5 slices/0 EBR remaining is insufficient evidence for full MMU fit.',
                'No hardware programming; Fmax is TRACE, external pin delays unconstrained.'])
    (ROOT/'docs/verification-cp37.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS CP37 evidence: five exact-source fits, CPU/FIS/FB regressions; production unchanged')


if __name__ == '__main__':
    main()
