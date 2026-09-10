#!/usr/bin/env python3
"""Archive CP35 context-hook evidence; distinguish probe, board and MMU scope."""
import json
import re
import shutil
from board_common import ROOT
from record_cp33 import digest, synthesis


def checked(name, key='inputs_sha256'):
    report = json.loads((ROOT/'build'/name).read_text())
    for path, expected in report[key].items():
        assert digest(ROOT/path) == expected, (name, path, 'test input changed')
    return report


def main():
    fits = {name: synthesis(name) for name in ('cp35a', 'cp35b', 'cp35c', 'cp35d', 'cp35e')}
    production = synthesis('cp31c', True)
    # The selected HDL/generator must match CP35c. Only the later historical
    # CP35d config was added to the shared synthesis launcher after that run.
    launcher_delta = []
    for name in ('cp35c', 'cp35e'):
        inputs = json.loads((ROOT/'synth/reports'/name/'inputs.json').read_text())['files']
        for path, expected in inputs.items():
            if path.startswith('generated:'):
                continue
            actual = ROOT/path
            if path == 'build/cp35e/uj11_board.v':
                actual = ROOT/'build/cp35-board/uj11_board.v'
            if name == 'cp35c' and path == 'tools/checkpoint.py':
                if digest(actual) != expected:
                    launcher_delta.append(path)
                continue
            assert digest(actual) == expected, (name, path, 'selected source changed')
    generated = checked('cp35/inputs.json')
    for path, expected in generated['outputs_sha256'].items():
        assert digest(ROOT/path) == expected, path
    assert generated['original_words_unchanged'] == 954 and generated['microcode_words'] == 963
    miter = [checked(f'cp35-miter-{sim}.json') for sim in ('verilator', 'iverilog')]
    assert [r['cases'] for r in miter] == [69632, 1024]
    assert len(miter[0]['memory_upcs']) == 88
    negative = checked('cp35-negative.json')
    assert len(negative['tests']) == 5 and all(t['rejected'] for t in negative['tests'])
    assert negative['assembler_rejections'] == 3
    fis = checked('cp35-fis.json')
    assert [t['cases'] for t in fis['tests']] == [23840, 23840, 645]
    for path, expected in fis['vendor_sha256'].items():
        assert digest(ROOT/'build/vendor'/path) == expected
    board = checked('cp35-entry-board.json', 'files')
    assert board['context_enabled'] and not board['hold'] and not board['translation']
    assert digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk') == board['image_sha256']
    log = (ROOT/'build/cp35-entry-board-rt11.log').read_text()
    match = re.search(r'PASS uJ11 cold RT-11 boot \+ DIR: (\d+) clocks, (\d+) RK commands, (\d+) timer edges, (\d+) UART wire bytes, (\d+) SD reads/(\d+) writes', log)
    assert match and 'FATAL' not in log
    counters = dict(zip(('clocks', 'rk_commands', 'timer_edges', 'uart_wire_bytes', 'sd_reads', 'sd_writes'), map(int, match.groups())))
    match = re.search(r'BOARD COUNTS retired(\d+) reads(\d+) writes(\d+) FRAM-CS(\d+)', log)
    assert match
    counters.update(zip(('retirements', 'read_beats', 'write_beats', 'fram_transactions'), map(int, match.groups())))
    logs = ['cp35-entry.log', 'cp35-final-lint.log', 'cp35-entry-board-rt11.log',
            'cp35-entry-board-build.log', 'cp35-entry-board.json', 'cp35-entry-uart.txt',
            'cp35-negative.json', 'cp35-fis.json']
    assert 'PASS MMU entry: 210246 cycles, all 1024 return PCs' in (ROOT/'build/cp35-entry.log').read_text()
    lint = (ROOT/'build/cp35-final-lint.log').read_text()
    assert 'PASS strict lint:' in lint and '%Warning' not in lint and '%Error' not in lint
    for result in miter:
        tag = 'cp35-miter-'+result['simulator']
        logs += [tag+'.json', tag+'.log', tag+'-build.log']
        assert all(line in (ROOT/'build'/(tag+'.log')).read_text() for line in result['pass_lines'])
    for test in negative['tests']:
        tag = 'cp35-negative-'+test['mutation']
        logs += [tag+'.log', tag+'-build.log']
        assert (ROOT/'build'/(tag+'.log')).read_text().strip() == test['failure']
    for test in fis['tests']:
        tag = test['tag']
        logs += [tag+'.log', tag+'-build.log']
        assert test['pass_line'] in (ROOT/'build'/(tag+'.log')).read_text()
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm) == xm_sha and xm.stat().st_size == 27540480
    out = ROOT/'tb/reports/cp35'
    out.mkdir(parents=True, exist_ok=True)
    for name in logs:
        shutil.copyfile(ROOT/'build'/name, out/name)
    sources = set(['Makefile', 'tools/record_cp35.py', 'tools/record_cp33.py',
                   'tools/check_mmu_entry_lint.py', 'tb/tb_mmu_entry.v'])
    for result in miter+[negative, fis, generated]:
        sources.update(p for p in result['inputs_sha256'] if not p.startswith('build/'))
    sources.update(p for p in board['files'] if not p.startswith('build/'))
    report = dict(
        checkpoint='CP35: automatic microcode entry/return before guest memory words',
        date='2026-09-10', synthesis=fits, production_cp31c=production,
        production_inputs_unchanged=True, production_has_mmu=False,
        board_programmed=False, physical_board_revision='CP29a',
        selected_candidate='cp35c', rejected_candidate='cp35d',
        selected_hdl_matches_frozen_synthesis=True, synthesis_launcher_metadata_changed=launcher_delta,
        production_microcode_words=954, candidate_microcode_words=963, candidate_free_words=61,
        entry_state_ff=12, entry_probe_harness_ff=30, core_probe_harness_ff=208,
        fixed_board_lut_delta=21, fixed_board_free_lut=7, fixed_board_free_slices=2,
        routine=dict(stored_words=9, executed_words=8, overhead_clocks_per_entry=9,
                     held_clocks_add_one_each=True, scratch=['T5', 'T6', 'T7'],
                     psw_restored=True, call_link_preserved=True, translates_addresses=False),
        tests=dict(unit_cycles=210246, unit_return_addresses=1024, strict_lint=True,
                   miter_pass_lines=[r['pass_lines'] for r in miter],
                   covered_memory_words=88, negative_controls=5, assembler_rejections=3,
                   fis=fis['tests'], rt11fb_cold_board=counters),
        rt11_xm=dict(path='lsi11/disks/rt11v5.3/system.dsk', sha256=xm_sha,
                     image_modified=False, boot_tested=False),
        sources_sha256={p: digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p)
                                  for name in fits for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        decision='Keep CP35c as an experimental context mechanism; reject masked-OR CP35d. '
                 'Reduce full-board area before connecting APR, translation or MMR.',
        limits=['CP35e ties enable=1 and hold=0; runtime MMR/APR controls may cost more.',
                'The helper deliberately exercises preservation, not a final MMU routine or CPI target.',
                'CPU comparison is a dynamic miter, not exhaustive ISA state-space proof.',
                'FIS SPI regression uses the existing 64 KiB guest interface, not high-memory CPU access.',
                'No CPU PAR/PDR lookup, MMR0/1/2/3, PA22 bus, MMU abort/restart or high-memory RK DMA.',
                'RT-11FB is a regression only; RT-11XM boot and integrated MMU fit remain pending.',
                'No FPGA programming. Production remains CP31c, hardware remains CP29a.'])
    (ROOT/'docs/verification-cp35.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(synthesis=fits, board=counters, production_unchanged=True, xm_boot_tested=False), indent=2))


if __name__ == '__main__':
    main()
