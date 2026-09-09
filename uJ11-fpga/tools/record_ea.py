#!/usr/bin/env python3
"""Archive CP7 evidence, requiring portable/vendor ROM results to agree."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]
MODES = {-1: 'ideal_ram', 0: 'legacy_fram', 1: 'sequential_fram', 2: 'prefetch_fram'}
LOGS = ['cp7-tests.log', 'cp7-vendor.log', 'cp7-benchmarks-portable.log',
        'cp7-benchmarks-vendor.log']


def read_json(path):
    return json.loads(path.read_text())


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def compare_results(stem, count):
    portable = read_json(ROOT / 'build' / f'{stem}-portable.json')
    vendor = read_json(ROOT / 'build' / f'{stem}.json')
    require(portable == vendor and len(portable) == count,
            f'{stem}: portable/vendor results differ or missing runs')
    return portable


def main():
    reports = ROOT / 'tb/reports'
    reports.mkdir(exist_ok=True)
    logs = {name: (ROOT / 'build' / name).read_text() for name in LOGS}
    for name, text in logs.items():
        require(not any(mark in text for mark in ['FATAL', 'FAILED', '%Error', '%Warning']),
                f'{name}: verification failure')
    common = ['PASS differential: 6272', 'PASS FRAM differential: 6272',
              'PASS EA differential mode-1: 13587', 'PASS EA differential mode2: 13587',
              'PASS EA directed:', 'PASS stream engine: opcode+3']
    for name in LOGS[:2]:
        for marker in common:
            require(marker in logs[name], f'{name}: missing {marker}')
    for marker in ['Ran 9 tests', 'PASS prefetch: 503', 'PASS lsi11 peripherals: 19',
                   'PASS stream hint: 65536', 'PASS prefetch control:',
                   'exactly 12544 supported encodings']:
        require(marker in logs[LOGS[0]], f'missing {marker}')

    # Every source archive is checked, including rejected synthesis experiments.
    archives = []
    for folder in sorted((ROOT / 'synth/reports').iterdir()):
        info = read_json(folder / 'inputs.json')
        with tarfile.open(folder / 'source.tgz') as archive:
            members = {m.name.removeprefix('./'): m for m in archive.getmembers()}
            for path, digest in info['files'].items():
                if path.startswith('generated:'):
                    data = (folder / path.split(':', 1)[1]).read_bytes()
                else:
                    data = archive.extractfile(members[path]).read()
                    if folder.name in ('cp7d', 'cp7e'):
                        require(sha256((ROOT / path).read_bytes()) == digest,
                                f'{folder.name}: current input changed: {path}')
                require(sha256(data) == digest, f'{folder.name}: archive mismatch: {path}')
        archives.append(folder.name)

    results = []
    for mode, label in MODES.items():
        for row in compare_results(f'ea-benchmarks-{mode}', 13):
            require(row['instructions'] == 256, 'unexpected EA retirement count')
            cpi = row['microclocks'] / row['instructions']
            results.append(dict(memory_mode=mode, memory_model=label, **row,
                                microclocks_per_instruction=cpi,
                                memory_beats_per_instruction=row['memory_beats'] / 256,
                                calculated_ips_at_29_56_mhz=29560000 / cpi))
    rr_ideal = compare_results('benchmarks', 15)
    rr_fram = [dict(memory_mode=mode, **row) for mode in range(3)
               for row in compare_results(f'fram-benchmarks-{mode}', 5)]

    # Keep the rejected policy as measured portable-simulation evidence, separate
    # from the final portable/vendor comparison. A clean rerun reuses its archive.
    rejected_path = reports / 'cp7-prefetch-uncontrolled.json'
    old_paths = [ROOT / 'build' / f'ea-benchmarks-{m}-uncontrolled.json' for m in MODES]
    if all(p.exists() for p in old_paths):
        write_json(rejected_path, {
            'method': 'Earlier portable-ROM simulation; unrestricted CP7c prefetch policy.',
            'rtl_synthesis_input_sha256': read_json(ROOT / 'synth/reports/cp7c/inputs.json')[
                'input_revision_sha256'],
            'results': [dict(memory_mode=m, **row) for m, path in zip(MODES, old_paths)
                        for row in read_json(path)]})

    cycles = {}
    for mode, label, total in [(-1, 'ram', 395656), (2, 'fram', 6021885)]:
        data = (ROOT / 'build' / f'ea-cycles-{mode}.csv').read_bytes()
        require(data == (ROOT / 'build' / f'ea-cycles-{mode}-portable.csv').read_bytes(),
                f'{label}: per-case portable/vendor cycles differ')
        rows = list(csv.DictReader(io.StringIO(data.decode())))
        require(len(rows) == 13587, f'{label}: missing differential cases')
        clocks = sum(int(row['microclocks']) for row in rows)
        beats = sum(int(row['memory_beats']) for row in rows)
        coverage = {(int(row['opcode'], 8) >> 12, (int(row['opcode'], 8) >> 9) & 7,
                     (int(row['opcode'], 8) >> 3) & 7) for row in rows}
        require(coverage == {(op, sm, dm) for op in [1, 2, 6]
                            for sm in range(8) for dm in range(8)},
                f'{label}: incomplete opcode/source/destination mode coverage')
        require(clocks == total and beats == 56057, f'{label}: unexpected totals')
        (reports / f'cp7-ea-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
        cycles[label] = dict(cases=len(rows), microclocks=clocks, memory_beats=beats,
                             mode_pairs=len(coverage), portable_vendor_equal=True)

    for name in LOGS:
        shutil.copyfile(ROOT / 'build' / name, reports / name)
    shutil.copyfile(ROOT / 'build/ea-oracle.log', reports / 'cp7-ea-oracle.log')
    fixture = (ROOT / 'build/ea-vectors.txt').read_bytes()
    (reports / 'cp7-ea-vectors.txt.gz').write_bytes(gzip.compress(fixture, mtime=0))

    write_json(ROOT / 'docs/benchmarks-cp7.json', {
        'date': '2026-09-08', 'clock_hz_nominal': 29560000, 'spi_divider': 1,
        'method': 'Functional RTL simulation, not a board benchmark; all final counts '
                  'match between portable ROM and vendor DP8KC.',
        'ea_loop': {'body_instructions': 31, 'tail': 'BR', 'warmup_retirements': 32,
                    'measured_retirements': 256, 'results': results},
        'rr_ram': {'measured_retirements': 4096, 'results': rr_ideal},
        'rr_fram': {'warmup_retirements': 64, 'measured_retirements': 512,
                    'results': rr_fram},
        'rejected_unrestricted_prefetch': read_json(rejected_path)
            if rejected_path.exists() else None,
        'differential_cycles': cycles})

    files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*.v',
                    'microcode/generated/*.mem', 'microasm/*.py', 'tb/*.v', 'tb/*.c',
                    'tb/test_*.py', 'reference/lsi11/*.v', 'tools/*.py',
                    'tb/reports/cp7*', 'synth/reports/cp7*/inputs.json',
                    'synth/reports/cp7*/result.json']:
        files.update(ROOT.glob(pattern))
    files.update(ROOT / p for p in ['Makefile', '../core/core.c', '../core/core.h',
                                    '../core/hardware.c', '../core/hardware.h',
                                    'docs/benchmarks-cp7.json'])
    vendor_models = {p.name: sha256(p.read_bytes()) for p in (ROOT / 'build/vendor').glob('*.v')}
    write_json(ROOT / 'docs/verification-cp7.json', {
        'date': '2026-09-08', 'isa': 'Word MOV/ADD/CMP modes 0..7, BR; no MMU',
        'checks': {'python_test_methods': 9, 'dcj11_rr_br_cases_each_memory_rom_pair': 6272,
                   'dcj11_ea_candidates': 13842, 'dcj11_ea_completed': 13587,
                   'dcj11_ea_trap_io_excluded': 255, 'ea_directed_cases': 7,
                   'decoder_encodings_checked': 65536, 'decoder_encodings_supported': 12544,
                   'dynamic_stream_hint_encodings': 65536, 'prefetch_protocol_beats': 503,
                   'legacy_peripheral_beats': 19, 'ea_benchmark_runs_each_rom_model': 52,
                   'rr_benchmark_runs_each_rom_model': 30, 'differential_cycles': cycles,
                   'synthesis_archives_verified': archives,
                   'current_inputs_equal_synthesis': ['cp7d', 'cp7e']},
        'limits': ['No byte ISA, architectural traps/interrupts, EIS or full J-11 ISA.',
                   'Abort partial-register state is not yet a DCJ11 compatibility claim.',
                   'Functional simulation and synthesis; no FPGA programming/board timing test.'],
        'fixture_uncompressed_sha256': sha256(fixture), 'vendor_models_sha256': vendor_models,
        'files': {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in sorted(files)}})
    print(f'Recorded CP7: {len(archives)} source archives; 13587 EA cases in four memory/ROM '
          'combinations; 82 benchmark runs per ROM model; exact counts agree.')


if __name__ == '__main__':
    main()
