#!/usr/bin/env python3
"""Archive CP8 tests, benchmark comparisons and exact synthesis provenance."""
import ast
import csv
import gzip
import io
import json
import shutil
import tarfile

from record_ea import ROOT, MODES, compare_results, read_json, require, sha256, write_json

LOGS = ['cp8-tests.log', 'cp8-vendor-base.log', 'cp8-vendor-ea.log',
        'cp8-benchmarks-portable.log', 'cp8-benchmarks-vendor.log']


def executable_ast(data):
    tree = ast.parse(data)
    if (tree.body and isinstance(tree.body[0], ast.Expr)
            and isinstance(tree.body[0].value, ast.Constant)
            and isinstance(tree.body[0].value.value, str)):
        tree.body.pop(0)
    return ast.dump(tree, include_attributes=False)


def main():
    reports = ROOT / 'tb/reports'
    logs = {name: (ROOT / 'build' / name).read_text() for name in LOGS}
    for name, text in logs.items():
        require(not any(x in text for x in ['FATAL', 'FAILED', '%Error', '%Warning']),
                f'{name}: verification failure')
    for name in LOGS[:2]:
        for marker in ['PASS differential: 12928', 'PASS FRAM differential: 12928',
                       'PASS stream engine: opcode+3']:
            require(marker in logs[name], f'{name}: missing {marker}')
    for name in [LOGS[0], LOGS[2]]:
        for marker in ['PASS EA differential mode-1: 31671',
                       'PASS EA differential mode2: 31671', 'PASS EA directed: 14 cases']:
            require(marker in logs[name], f'{name}: missing {marker}')
    for marker in ['Ran 10 tests', '4096 pair checks', 'PASS prefetch: 503',
                   'PASS lsi11 peripherals: 19', 'exactly 28928 supported encodings']:
        require(marker in logs[LOGS[0]], f'missing {marker}')

    archives, banner_deltas = [], []
    for folder in sorted((ROOT / 'synth/reports').iterdir()):
        info = read_json(folder / 'inputs.json')
        with tarfile.open(folder / 'source.tgz') as archive:
            members = {m.name.removeprefix('./'): m for m in archive.getmembers()}
            for path, digest in info['files'].items():
                if path.startswith('generated:'):
                    data = (folder / path.split(':', 1)[1]).read_bytes()
                else:
                    data = archive.extractfile(members[path]).read()
                    if folder.name in ['cp8d', 'cp8c-retrace39']:
                        current = (ROOT / path).read_bytes()
                        if sha256(current) != digest:
                            require(path == 'microasm/uj11asm.py'
                                    and executable_ast(current) == executable_ast(data),
                                    f'{folder.name}: changed synthesis input {path}')
                            banner_deltas.append(dict(checkpoint=folder.name, path=path,
                                archive_sha256=digest, current_sha256=sha256(current),
                                reason='Module docstring corrected v3 to v4 after synthesis; '
                                       'executable AST and generated ROM are identical.'))
                require(sha256(data) == digest, f'{folder.name}: archive mismatch: {path}')
        if 'placement' in info:
            require(sha256((folder / 'design.ncd').read_bytes()) == info['placement']['ncd_sha256'],
                    f'{folder.name}: placed netlist mismatch')
        archives.append(folder.name)

    results = []
    for mode, label in MODES.items():
        for row in compare_results(f'ea-benchmarks-{mode}', 25):
            require(row['instructions'] == 256, 'unexpected EA retirement count')
            cpi = row['microclocks'] / 256
            results.append(dict(memory_mode=mode, memory_model=label, **row,
                                microclocks_per_instruction=cpi,
                                memory_beats_per_instruction=row['memory_beats'] / 256,
                                calculated_ips_at_29_56_mhz=29560000 / cpi))
    rr_ram = compare_results('benchmarks', 27)
    rr_fram = [dict(memory_mode=m, **row) for m in range(3)
               for row in compare_results(f'fram-benchmarks-{m}', 9)]
    previous = read_json(ROOT / 'docs/benchmarks-cp7.json')
    for old in previous['ea_loop']['results']:
        new = next(r for r in results if r['memory_mode'] == old['memory_mode']
                   and r['workload'] == old['workload'])
        require(old == new, f'CP7 EA benchmark regression: {old["workload"]}')
    require(rr_ram[:15] == previous['rr_ram']['results'], 'CP7 RAM RR regression')
    for old in previous['rr_fram']['results']:
        require(old in rr_fram, f'CP7 FRAM RR regression: {old["workload"]}')

    cycles = {}
    for mode, label, total in [(-1, 'ram', 948247), (2, 'fram', 14739672)]:
        data = (ROOT / 'build' / f'ea-cycles-{mode}.csv').read_bytes()
        require(data == (ROOT / 'build' / f'ea-cycles-{mode}-portable.csv').read_bytes(),
                f'{label}: per-case portable/vendor cycles differ')
        rows = list(csv.DictReader(io.StringIO(data.decode())))
        require(len(rows) == 31671, f'{label}: missing cases')
        coverage = {(int(r['opcode'], 8) >> 12, (int(r['opcode'], 8) >> 9) & 7,
                     (int(r['opcode'], 8) >> 3) & 7) for r in rows}
        require(coverage == {(op, sm, dm) for op in [1, 2, 3, 4, 5, 6, 14]
                            for sm in range(8) for dm in range(8)}, 'missing mode pairs')
        clocks = sum(int(r['microclocks']) for r in rows)
        beats = sum(int(r['memory_beats']) for r in rows)
        require(clocks == total and beats == 137006, f'{label}: unexpected totals')
        (reports / f'cp8-ea-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
        cycles[label] = dict(cases=len(rows), microclocks=clocks, memory_beats=beats,
                             mode_pairs=len(coverage), portable_vendor_equal=True)

    for name in LOGS:
        shutil.copyfile(ROOT / 'build' / name, reports / name)
    for name in ['cp8-pair-tests.log', 'cp8-initial-tests.log']:
        shutil.copyfile(ROOT / 'build' / name, reports / name)
    shutil.copyfile(ROOT / 'build/ea-oracle.log', reports / 'cp8-ea-oracle.log')
    fixtures = {}
    for source, target in [('ea-vectors.txt', 'cp8-ea-vectors.txt.gz'),
                           ('isa_vectors.mem', 'cp8-rr-vectors.mem.gz')]:
        data = (ROOT / 'build' / source).read_bytes()
        (reports / target).write_bytes(gzip.compress(data, mtime=0))
        fixtures[source] = sha256(data)

    write_json(ROOT / 'docs/benchmarks-cp8.json', {
        'date': '2026-09-08', 'clock_hz_nominal': 29560000, 'spi_divider': 1,
        'method': 'Functional RTL simulation, not a board benchmark; all counts '
                  'match between portable ROM and vendor DP8KC.',
        'ea_loop': {'body_instructions': 31, 'tail': 'BR', 'warmup_retirements': 32,
                    'measured_retirements': 256, 'results': results},
        'rr_ram': {'warmup_retirements': 64, 'measured_retirements': 4096, 'results': rr_ram},
        'rr_fram': {'warmup_retirements': 64, 'measured_retirements': 512, 'results': rr_fram},
        'cp7_benchmark_counts_unchanged': True, 'differential_cycles': cycles})
    files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*.v',
                    'microcode/generated/*.mem', 'microasm/*.py', 'tb/*.v', 'tb/*.c',
                    'tb/test_*.py', 'reference/lsi11/*.v', 'tools/*.py',
                    'tb/reports/cp8*', 'synth/reports/cp8*/inputs.json',
                    'synth/reports/cp8*/result.json', 'synth/reports/cp8c-retrace39/retrace.tcl',
                    'synth/reports/cp8c-retrace39/design.prf']:
        files.update(ROOT.glob(pattern))
    files.update(ROOT / p for p in ['Makefile', '../core/core.c', '../core/core.h',
                                    '../core/hardware.c', '../core/hardware.h',
                                    'docs/benchmarks-cp8.json'])
    write_json(ROOT / 'docs/verification-cp8.json', {
        'date': '2026-09-08', 'isa': 'Word MOV/CMP/BIT/BIC/BIS/ADD/SUB modes 0..7, BR; no MMU',
        'checks': {'python_test_methods': 10, 'datapath_pair_checks': 4096,
                   'dcj11_rr_br_cases_each_memory_rom_pair': 12928,
                   'dcj11_ea_candidates': 32298, 'dcj11_ea_completed': 31671,
                   'dcj11_ea_excluded': {'total': 627, 'abort': 595, 'io': 32},
                   'ea_directed_cases': 14, 'decoder_encodings_checked': 65536,
                   'decoder_encodings_supported': 28928, 'prefetch_protocol_beats': 503,
                   'legacy_peripheral_beats': 19, 'ea_benchmark_runs_each_rom_model': 100,
                   'rr_benchmark_runs_each_rom_model': 54, 'differential_cycles': cycles,
                   'synthesis_archives_verified': archives,
                   'current_rtl_and_rom_equal_synthesis': ['cp8d', 'cp8c-retrace39'],
                   'nonfunctional_assembler_changes': banner_deltas},
        'limits': ['No byte ISA, architectural traps/interrupts, EIS or full J-11 ISA.',
                   'Abort partial-register state is not yet a DCJ11 compatibility claim.',
                   'No FPGA programming/board timing test.',
                   'Core 39 MHz PASS reuses CP8c routing; new placements at 40 and 39 MHz failed.'],
        'fixtures_uncompressed_sha256': fixtures,
        'vendor_models_sha256': {p.name: sha256(p.read_bytes())
                                for p in (ROOT / 'build/vendor').glob('*.v')},
        'files': {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in sorted(files)}})
    print(f'Recorded CP8: {len(archives)} synthesis archives, 31671 EA and 12928 RR/BR cases; '
          '154 benchmark runs per ROM model agree; CP7 benchmark counts unchanged.')


if __name__ == '__main__':
    main()
