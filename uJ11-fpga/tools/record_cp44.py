#!/usr/bin/env python3
"""Archive CP44 relocation proof and rejected full-board area experiments."""
import gzip
import json
import re
import shutil
import tarfile
from board_common import ROOT
from record_cp33 import digest, synthesis
from record_cp36 import board_counts
from record_cp37 import checked, failed_fit


def main():
    fits = {f'cp44{x}': failed_fit(f'cp44{x}') for x in 'abcde'}
    assert [(r['lut4'], r['ff'], r['ebr'], r['slices']) for r in fits.values()] == [
        (1394, 373, 7, 699), (1351, 359, 7, 676), (1368, 356, 7, 685),
        (1351, 359, 7, 676), (1351, 359, 7, 676)]
    final_inputs = json.loads((ROOT/'synth/reports/cp44e/inputs.json').read_text())['files']
    sources = {p for p in final_inputs if not p.startswith('generated:')}
    for p in sources:
        assert digest(ROOT/p) == final_inputs[p], (p, 'final candidate input changed')
    production = synthesis('cp40h'); accepted_apr = synthesis('cp43d')
    for name in ('cp40h', 'cp43d'):
        inputs = json.loads((ROOT/f'synth/reports/{name}/inputs.json').read_text())['files']
        for p, h in inputs.items():
            if not p.startswith('generated:') and p.endswith(('.v', '.mem', '.lpf', '.sty')):
                assert digest(ROOT/p) == h, (name, p, 'accepted hardware changed')
    logs = []
    for variant, words in (('relocate', 970), ('direct', 954)):
        name = f'cp44-{variant}/inputs.json'; build = checked(name)
        assert build['microcode_words'] == words
        if variant == 'direct': assert not build['classified']
        for p, h in build['outputs_sha256'].items(): assert digest(ROOT/p) == h, p
        sources.update(build['inputs_sha256']); sources.update(build['outputs_sha256'])
        logs.append(name)
    sources.update(['tools/record_cp44.py', 'tools/record_cp33.py', 'tools/record_cp36.py',
                    'tools/record_cp37.py', 'tools/archive_synthesis.py', 'tools/run_board.py'])
    tests = {}
    suites = ['direct-cpu-portable-4096', 'cpu-portable-4096',
              'direct-cpu-vendor-4', 'cpu-vendor-4', 'direct-cpu-portable-edges',
              'direct-cpu-vendor-edges', 'cpu-portable-edges', 'bus']
    for suite in suites:
        tag = 'cp44-'+suite; result = checked(tag+'.json')
        sources.update(result['inputs_sha256'])
        text = (ROOT/f'build/{tag}.log').read_text()
        assert result['pass_lines'] and all(s in text for s in result['pass_lines'])
        assert 'FATAL' not in text
        tests[suite] = result['pass_lines']
        logs.extend(tag+s for s in ('.json', '.log', '-build.log'))
    assert '67468380 clocks 8 control beats 590096 PAR reads' in tests[suites[0]][0]
    assert '76319820 clocks 8 control beats 590096 PAR reads' in tests[suites[1]][0]
    assert 76319820-67468380 == 15*590096
    assert '97692 clocks 8 control beats 848 PAR reads' in tests[suites[2]][0]
    assert '110412 clocks 8 control beats 848 PAR reads' in tests[suites[3]][0]
    assert '6291456 combinations' in tests['bus'][1]
    units = checked('cp44-units.json'); sources.update(units['inputs_sha256'])
    assert units['strict_lint_modules'] == 3
    logs.extend(['cp44-units.json', 'cp44-units-lint.log'])
    lint = (ROOT/'build/cp44-units-lint.log').read_text()
    assert '%Warning' not in lint and '%Error' not in lint
    for name in ('relocate', 'mmr0'):
        log = f'cp44-{name}-cc.log'; assert not (ROOT/'build'/log).read_text(); logs.append(log)
    for test in units['tests']:
        tag = test['tag']; text = (ROOT/f'build/{tag}.log').read_text()
        assert test['pass_line'] in text and 'FATAL' not in text
        assert digest(ROOT/test['corpus']) == test['corpus_sha256']
        tests[tag] = [test['pass_line']]
        logs.extend(tag+s for s in ('.log', '-build.log'))
    assert '262144 addresses, 196608 stalled lookup edges' in units['tests'][0]['pass_line']
    assert '262160 commands, 16 RESET' in units['tests'][1]['pass_line']
    board = checked('cp44-board-verified.json', 'files'); sources.update(board['files'])
    assert board['kernel_relocation'] and not board['pdr_protection'] and not board['rt11_xm']
    text = (ROOT/'build/cp44-board-rt11.log').read_text(); counts = board_counts(text)
    assert counts == dict(clocks=415159611, rk_commands=300, timer_edges=576,
                         uart_wire_bytes=3270, sd_reads=162, sd_writes=6,
                         retirements=4311823, read_beats=5675704, write_beats=489280,
                         fram_transactions=3980328)
    match = re.search(r'MMU COUNTS mapped(\d+) highRAM(\d+) MMR0writes(\d+) enabledDMA(\d+) enabledPrivateROM(\d+)', text)
    assert match and list(map(int, match.groups())) == [526239, 65664, 4, 0, 0]
    mmu_counts = dict(zip(('mapped_beats', 'upper_fram_beats', 'mmr0_writes',
                          'dma_beats_while_enabled', 'private_rom_beats_while_enabled'), map(int, match.groups())))
    assert digest(ROOT/'build/cp44-uart.txt') == digest(ROOT/'tb/reports/cp43/cp43-uart.txt')
    assert board['image_sha256'] == digest(ROOT/'../lsi11-fpga/images/rt11v503.dsk')
    logs.extend('cp44'+s for s in ('-board-verified.json', '-board-inputs.json', '-board-build.log', '-board-rt11.log', '-uart.txt'))
    xm = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(xm) == xm_sha and xm.stat().st_size == 27540480
    # Include the exact legacy WIDTH-scoped reference copies compiled by Verilator.
    sources.update(str(p.relative_to(ROOT)) for p in (ROOT/'build/cp44-test-reference').glob('*.v'))
    out = ROOT/'tb/reports/cp44'; out.mkdir(parents=True, exist_ok=True)
    for name in logs:
        dest = out/name; dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/'build'/name, dest)
    for test, rows in zip(units['tests'], (262144, 262160)):
        corpus = (ROOT/test['corpus']).read_bytes(); assert len(corpus.splitlines()) == rows
        (out/(test['corpus'].split('/')[-1]+'.gz')).write_bytes(gzip.compress(corpus, mtime=0))
    with tarfile.open(out/'test-sources.tgz', 'w:gz') as archive:
        for p in sorted(sources):
            if p.startswith(('/', 'build/vendor/', 'synth/reports/')) or p.endswith('-oracle.txt'): continue
            name = 'reference-repository/'+p[3:] if p.startswith('../') else p
            archive.add(ROOT/p, arcname=name)
    manual = '../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'
    report = dict(checkpoint='CP44: kernel-unified CPU relocation proof; every full-HC1200 fit rejected',
        date='2026-09-10', synthesis=fits, best_area_candidate='cp44e', adopted=False,
        best_candidate_inputs_match_current=True, production_cp40h=production, accepted_apr_cp43d=accepted_apr,
        accepted_hardware_inputs_unchanged=True, physical_board='CP29a', board_programmed=False,
        area_over_capacity=dict(lut4=71, slices=36), spare_ebr=0, fmax_mhz=None,
        microcode_words=dict(production=954, direct=954, microcoded=970, helper=16),
        mapped_beat_overhead_clocks=dict(direct=2, microcoded=17, external_holds_excluded=True),
        tests=tests, strict_unit_lint=True, cold_rt11fb=counts, cold_rt11fb_mmu=mmu_counts,
        fb_uart_matches_cp43=True, fb_image_sha256=board['image_sha256'], fis_full_corpus_rerun=False,
        rt11_xm=dict(boot_tested=False, image_modified=False, sha256=xm_sha),
        manual=dict(path=manual, sha256=digest(ROOT/manual), sections=['4.5', '4.7.1', '4.7.4']),
        sources_sha256={p: digest(ROOT/p) for p in sorted(sources)},
        archived_tests_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
        archived_synthesis_sha256={str(p.relative_to(ROOT)): digest(p) for n in fits for p in sorted((ROOT/'synth/reports'/n).iterdir())},
        limits=['Kernel unified PAR relocation only; no PDR protection/automatic W or hardware MMR0 fault/page metadata.',
                'MMR0 software bits 15:13 and 0 only; no MMR1/2, abort250, freeze/restart, processor modes, I/D, CSM or MAP.',
                'C oracle compares relocation with valid PDR and software MMR0 controls; no complete CPU/MMU differential.',
                'RK private MOVB DMA remains physical low64KiB. High DMA and an active-MMU RK transfer are not verified.',
                'Cold FB uses mapped memory/high FRAM, but no private ROM/DMA accesses while MMU is enabled.',
                'Native CPU/ALU/microcode unchanged in direct candidate; full FIS corpus not rerun, nor claimed for microcoded candidate.',
                'Every full-board MAP fails area; no PAR/TRACE, FPGA image, Fmax or RT-11XM result.'])
    (ROOT/'docs/verification-cp44.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS CP44 evidence: five rejected fits; upper FRAM CPU/edges/vendor/C/bus/cold FB; accepted builds unchanged')


if __name__ == '__main__': main()
