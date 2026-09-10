#!/usr/bin/env python3
"""Bind CP32 tests to inputs and archive measured, isolated MMU18/22 results."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'Makefile', 'rtl/uj11_mmu_translate.v', 'rtl/uj11_mmu_translate18.v',
    'synth/machxo2/uj11_probe_mmu.v', 'tb/tb_mmu_translate.v',
    'tb/tb_mmu_oracle.v', 'tb/reference_mmu_translate.c', 'tb/tb_mmu_fram.v',
    'boards/hc1200/uj11_board_fram.v', 'reference/lsi11/spi_fram_model.v',
    'tools/run_mmu_fram.py', 'tools/record_cp32.py',
    '../core/core.c', '../core/core.h', '../core/pdp11_fp.c',
    '../core/hardware.c', '../core/hardware.h',
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_hashes():
    return {p: digest(ROOT/p) for p in SOURCES}


def check_synthesis(name, current):
    directory = ROOT/'synth/reports'/name
    result = json.loads((directory/'result.json').read_text())
    assert result['fully_routed'] and result['timing_pass'] and result['diamond_returncode'] == 0
    assert json.loads((directory/'inputs.json').read_text()) == result['inputs']
    with tarfile.open(directory/'source.tgz') as archive:
        for path, expected in result['inputs']['files'].items():
            generated = path.startswith('generated:')
            data = ((directory/'clock.lpf').read_bytes() if generated else
                    archive.extractfile(path).read())
            assert hashlib.sha256(data).hexdigest() == expected, (name, path, 'snapshot')
            if current and not generated:
                assert digest(ROOT/path) == expected, (name, path, 'current input')
    for suffix, expected in result['reports'].items():
        assert digest(directory/('design'+suffix)) == expected, (name, suffix)
    return {k: result[k] for k in ('lut4', 'ff', 'ebr', 'slices', 'fmax_mhz', 'timing_pass')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='run tests with before/after input hashes')
    args = parser.parse_args()
    build = ROOT/'build'
    build.mkdir(exist_ok=True)
    log_names = ['cp32-translate.log', 'cp32-oracle.log', 'cp32-fram.log']
    if args.run:
        before = input_hashes()
        commands = [['make', 'test-mmu-translate'], ['make', '-B', 'test-mmu-oracle'],
                    ['make', 'test-mmu-fram']]
        for command, name in zip(commands, log_names):
            print(' '.join(command), flush=True)
            with (build/name).open('w') as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            print('\n'.join(line for line in (build/name).read_text().splitlines()
                            if line.startswith('PASS ')), flush=True)
        assert before == input_hashes(), 'test sources changed while running'
        (build/'cp32-test-inputs.json').write_text(json.dumps(before, indent=2)+'\n')
    inputs = json.loads((build/'cp32-test-inputs.json').read_text())
    assert inputs == input_hashes(), 'tests do not match current sources; use --run'
    fram_inputs = json.loads((build/'cp32-mmu-fram-inputs.json').read_text())
    assert all(inputs[p] == h for p, h in fram_inputs.items())
    for name, expected in zip(log_names, [
            ['3114656 checks FULL=0', '36144800 checks FULL=1'],
            ['262144 cases, 151902 translated / 110242 faults'],
            ['413 beats FULL=0, 50400 clocks', '196634 beats FULL=1, 24774246 clocks']]):
        log = (build/name).read_text()
        assert all(s in log for s in expected), (name, 'incomplete results')
    synthesis = {name: check_synthesis(name, name == 'cp32b') for name in ('cp32a', 'cp32b')}
    # No production RTL, firmware, microcode, or board build input has changed.
    # Its existing complete fit remains evidence for that unchanged baseline.
    production = check_synthesis('cp31c', True)
    stats = json.loads((ROOT/'microcode/generated/m0.stats.json').read_text())
    assert stats['used_words'] == 954 and stats['encoding_version'] == 12
    image = ROOT/'../lsi11/disks/rt11v5.3/system.dsk'
    xm_sha = '9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd'
    assert digest(image) == xm_sha and image.stat().st_size == 27540480
    out = ROOT/'tb/reports/cp32'
    out.mkdir(parents=True, exist_ok=True)
    log_names += ['cp32-test-inputs.json', 'cp32-mmu-fram-inputs.json',
                  'cp32-mmu-fram-four-state-build.log', 'cp32-mmu-fram-full-build.log',
                  'cp32-mmu-fram-four-state.log', 'cp32-mmu-fram-full.log']
    for name in log_names:
        shutil.copyfile(build/name, out/name)
    corpus = (build/'mmu-oracle.txt').read_bytes()
    assert len(corpus.splitlines()) == 262144
    (out/'mmu-oracle.txt.gz').write_bytes(gzip.compress(corpus, mtime=0))
    report = dict(
        checkpoint='CP32: isolated 18/22-bit translation and SPI FRAM integration harness',
        date='2026-09-10', synthesis=synthesis, unchanged_production_cp31c=production,
        production_inputs_match_cp31c=True, production_has_mmu=False,
        board_programmed=False, physical_board_revision='CP29a',
        fp11_removed=True, fis_retained=True, microcode_words=954,
        microcode_free=70, microcode_width=36, encoding_version=12,
        tests=dict(four_state_translation_checks=3114656, full_translation_checks=36144800,
                   c_oracle_cases=262144, c_oracle_successes=151902, c_oracle_faults=110242,
                   fram_four_state_beats=413, fram_four_state_clocks=50400,
                   fram_full_beats=196634, fram_full_clocks=24774246,
                   strict_translator_lint=True, physical_bytes_compared=131072),
        oracle=dict(source='../core/core.c:translate_va_ex', enable_mmu=1, model='DCJ11',
                    corpus_sha256=hashlib.sha256(corpus).hexdigest(),
                    compared='success/fault and successful canonical PA22; independent DEC checks cover simultaneous abort flags',
                    tables_and_modes='selected PAR/PDR supplied directly to RTL; no hardware table or processor-mode implementation claim'),
        rt11_xm=dict(repository_path='lsi11/disks/rt11v5.3/system.dsk', bytes=27540480,
                     sectors=53790, sha256=xm_sha, image_modified=False, boot_tested=False,
                     reason='CPU/MMR/PAR-PDR store/abort/restart and extended RK DMA not integrated'),
        manual=dict(local_path='../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf',
                    sha256=digest(ROOT/'../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf'),
                    sections=['4.5.2', '4.7.4.3', 'Figures 4-11..4-13']),
        inputs_sha256=inputs,
        archived_files_sha256={str(p.relative_to(ROOT)): digest(p) for p in sorted(out.iterdir())},
        synthesis_files_sha256={str(p.relative_to(ROOT)): digest(p)
                                for name in ('cp32a', 'cp32b')
                                for p in sorted((ROOT/'synth/reports'/name).iterdir())},
        limits=['Translator is combinational; all 80 probe FF are measurement harness.',
                'Probe LUT and Fmax do not establish integrated board area or timing.',
                'Only 28 LUT / 12 slices / 1 EBR free in unchanged CP31c; full MMU fit unproven.',
                'SPI test connects translator to real board transport, but supplies PAR/PDR directly.',
                'No CPU/MMR CSR/W-bit/restart/odd-vs-MMU fault priority or high-memory RK DMA test.',
                'No RT-11XM or physical-board test in this checkpoint.',
                'Imported SPI model width warnings suppressed only in frozen test copy; new RTL uses normal fatal warnings.'])
    (ROOT/'docs/verification-cp32.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(synthesis=synthesis, production_unchanged=True, xm_boot_tested=False), indent=2))


if __name__ == '__main__':
    main()
