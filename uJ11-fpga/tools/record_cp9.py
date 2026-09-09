#!/usr/bin/env python3
"""Archive CP9 correctness, FRAM policy experiments and fitted source provenance."""
import csv
import gzip
import io
from pathlib import Path
import shutil
import tarfile

from record_ea import ROOT, MODES, compare_results, read_json, require, sha256, write_json

LOGS = ['cp9-tests.log', 'cp9-vendor-base.log', 'cp9-vendor-isa.log',
        'cp9-benchmarks-portable.log', 'cp9-policy-benchmarks-portable.log',
        'cp9-benchmarks-vendor.log']
COUNTS = {'ea': (31671, 137006), 'single': (7080, 19588), 'branch': (7440, 7440)}


def main():
    reports = ROOT / 'tb/reports'
    logs = {n: (ROOT / 'build' / n).read_text() for n in LOGS}
    for name, text in logs.items():
        require(not any(x in text for x in ['FATAL', 'FAILED', '%Error', '%Warning']), name)
    for name in LOGS[:2]:
        for marker in ['PASS differential: 12928', 'PASS FRAM differential: 12928',
                       'PASS stream engine: opcode+3']:
            require(marker in logs[name], f'{name}: {marker}')
    for name in [LOGS[0], LOGS[2]]:
        for suite, (count, _) in COUNTS.items():
            for mode in [-1, 2]:
                marker = f'PASS {suite} differential mode{mode}: {count}'
                require(marker in logs[name], f'{name}: {marker}')
        for marker in ['PASS EA directed: 14', 'PASS single directed: 46']:
            require(marker in logs[name], f'{name}: {marker}')
    for marker in ['Ran 10 tests', '4096 pair checks', 'PASS prefetch: 503',
                   'PASS lsi11 peripherals: 19', 'exactly 33280 supported encodings']:
        require(marker in logs[LOGS[0]], f'missing {marker}')

    archives, report_count = [], 0
    for folder in sorted((ROOT / 'synth/reports').iterdir()):
        info = read_json(folder / 'inputs.json')
        with tarfile.open(folder / 'source.tgz') as archive:
            members = {m.name.removeprefix('./'): m for m in archive.getmembers()}
            for path, digest in info['files'].items():
                if path.startswith('generated:'):
                    data = (folder / path.split(':', 1)[1]).read_bytes()
                else:
                    data = archive.extractfile(members[path]).read()
                    if folder.name in ['cp9e', 'cp9f']:
                        require(sha256((ROOT / path).read_bytes()) == digest,
                                f'{folder.name}: current synthesis input changed: {path}')
                require(sha256(data) == digest, f'{folder.name}: archive mismatch: {path}')
        if 'placement' in info:
            require(sha256((folder / 'design.ncd').read_bytes()) == info['placement']['ncd_sha256'],
                    f'{folder.name}: NCD mismatch')
        if (folder / 'result.json').exists():
            result = read_json(folder / 'result.json')
            for key, digest in result.get('reports', {}).items():
                suffix = Path(key).suffix or key
                require(sha256((folder / ('design' + suffix)).read_bytes()) == digest,
                        f'{folder.name}: raw report mismatch: {key}')
                report_count += 1
        archives.append(folder.name)

    cycles, fixtures = {}, {}
    for suite, (count, beats) in COUNTS.items():
        cycles[suite] = {}
        for mode, label in [(-1, 'ram'), (2, 'fram')]:
            data = (ROOT / 'build' / f'{suite}-cycles-{mode}.csv').read_bytes()
            require(data == (ROOT / 'build' / f'{suite}-cycles-{mode}-portable.csv').read_bytes(),
                    f'{suite}/{label}: per-case portable/vendor mismatch')
            rows = list(csv.DictReader(io.StringIO(data.decode())))
            require(len(rows) == count, f'{suite}: case count')
            total_beats = sum(int(r['memory_beats']) for r in rows)
            require(total_beats == beats, f'{suite}: bus count')
            if suite == 'ea':
                require(data == gzip.decompress((reports / f'cp8-ea-cycles-{label}.csv.gz').read_bytes()),
                        'CP8 EA per-case regression')
            (reports / f'cp9-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
            cycles[suite][label] = dict(cases=count, microclocks=sum(int(r['microclocks']) for r in rows),
                                       memory_beats=beats, portable_vendor_equal=True)
        data = (ROOT / 'build' / f'{suite}-vectors.txt').read_bytes()
        if suite == 'ea':
            require(data == gzip.decompress((reports / 'cp8-ea-vectors.txt.gz').read_bytes()),
                    'CP8 fixture changed')
        (reports / f'cp9-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data, mtime=0))
        fixtures[suite] = sha256(data)
        shutil.copyfile(ROOT / 'build' / f'{suite}-oracle.log', reports / f'cp9-{suite}-oracle.log')

    previous = read_json(ROOT / 'docs/benchmarks-cp8.json')
    require(compare_results('benchmarks', 27) == previous['rr_ram']['results'], 'CP8 RR RAM regression')
    rr_fram = [dict(memory_mode=m, **row) for m in range(3)
               for row in compare_results(f'fram-benchmarks-{m}', 9)]
    require(rr_fram == previous['rr_fram']['results'], 'CP8 RR FRAM regression')
    old_ea, new_loops, policies = [], [], []
    for mode, label in MODES.items():
        for row in compare_results(f'ea-benchmarks-{mode}', 25):
            cpi = row['microclocks'] / row['instructions']
            old_ea.append(dict(memory_mode=mode, memory_model=label, **row,
                microclocks_per_instruction=cpi, memory_beats_per_instruction=row['memory_beats']/row['instructions'],
                calculated_ips_at_29_56_mhz=29560000/cpi))
        rows = compare_results(f'cp9-benchmarks-{mode}', 69)
        before = read_json(reports / 'cp9-prefetch-unrestricted' / f'benchmarks-{mode}.json')
        for index, row in enumerate(rows):
            cpi = row['microclocks'] / row['instructions']
            new_loops.append(dict(memory_mode=mode, memory_model=label, **row,
                microclocks_per_instruction=cpi, memory_beats_per_instruction=row['memory_beats']/row['instructions'],
                calculated_ips_at_29_56_mhz=29560000/cpi))
            old = before[index]
            require(old['instructions'] == row['instructions'], 'policy workload mismatch')
            policies.append(dict(memory_mode=mode, workload=row['workload'],
                unrestricted_microclocks=old['microclocks'], final_microclocks=row['microclocks'],
                difference_microclocks=row['microclocks']-old['microclocks'],
                unrestricted_spi_clocks=old['spi_clocks'], final_spi_clocks=row['spi_clocks']))
    require(old_ea == previous['ea_loop']['results'], 'CP8 EA benchmark regression')
    write_json(ROOT / 'docs/benchmarks-cp9.json', {
        'date': '2026-09-08', 'clock_hz_nominal': 29560000, 'spi_divider': 1,
        'method': 'RTL functional simulation, portable and vendor DP8KC counts equal; not a board benchmark.',
        'workloads': {'unary': '31 operations + BR; 32 warmup, 256 measured',
                      'branch_outcomes': '31 same-outcome branches + BR; 32 warmup, 256 measured',
                      'branch_taken_self': '1 warmup, 256 measured',
                      'countdown': 'MOV #16,R1; DEC R1; BNE DEC; BR start; 34 warmup, 272 measured'},
        'results': new_loops, 'prefetch_policy_comparison': policies,
        'cp8_all_154_benchmark_runs_unchanged': True, 'differential_cycles': cycles})
    for name in LOGS: shutil.copyfile(ROOT / 'build' / name, reports / name)
    data = (ROOT / 'build/isa_vectors.mem').read_bytes()
    require(data == gzip.decompress((reports / 'cp8-rr-vectors.mem.gz').read_bytes()), 'CP8 RR fixture changed')
    fixtures['rr'] = sha256(data)
    files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*.v', 'microcode/generated/*.mem',
                    'microasm/*.py', 'tb/*.v', 'tb/*.c', 'tb/*.h', 'tb/test_*.py', 'reference/lsi11/*.v',
                    'tools/*.py', 'tb/reports/cp9*', 'tb/reports/cp9-prefetch-unrestricted/*',
                    'synth/reports/cp9*/inputs.json', 'synth/reports/cp9*/result.json']:
        files.update(p for p in ROOT.glob(pattern) if p.is_file())
    files.update(ROOT/p for p in ['Makefile', '../core/core.c', '../core/core.h', '../core/hardware.c',
                                  '../core/hardware.h', 'docs/benchmarks-cp9.json'])
    write_json(ROOT / 'docs/verification-cp9.json', {
        'date': '2026-09-08', 'encoding_version': 4, 'microcode_words': 342,
        'isa': 'Seven word double-operand classes, 12 unary word classes, all 15 branch classes; no MMU.',
        'checks': {'python_test_methods': 10, 'alu_checks': 66592, 'datapath_pair_checks': 4096,
            'dcj11_rr_br_cases': 12928, 'dcj11_ea_candidates': 32298, 'dcj11_ea_completed': 31671,
            'dcj11_ea_excluded': {'total': 627, 'abort': 595, 'io': 32},
            'dcj11_single_candidates': 7104, 'dcj11_single_completed': 7080,
            'dcj11_single_excluded': {'total': 24, 'abort': 24},
            'dcj11_branch_cases': 7440, 'branch_flags_combinations': 240,
            'branch_offset_values_each_opcode': 256, 'ea_directed_cases': 14, 'single_directed_cases': 46,
            'decoder_encodings_checked': 65536, 'decoder_encodings_supported': 33280,
            'prefetch_protocol_beats': 503, 'legacy_peripheral_beats': 19,
            'benchmark_runs_each_rom_model': 430, 'cp8_benchmarks_unchanged': True,
            'differential_cycles': cycles, 'synthesis_archives_verified': archives,
            'raw_report_hashes_verified': report_count, 'current_inputs_equal_synthesis': ['cp9e', 'cp9f']},
        'limits': ['No byte ISA, JMP/JSR/RTS/SOB, SWAB/SXT/MARK, architectural traps/interrupts, EIS or J-11 banking.',
                   'Abort partial-register state is not yet a DCJ11 compatibility claim.',
                   'No board programming, full peripheral synthesis top or external pin timing closure.',
                   'Branch offset and flag coverage is not the exhaustive Cartesian product.'],
        'fixtures_uncompressed_sha256': fixtures,
        'vendor_models_sha256': {p.name: sha256(p.read_bytes()) for p in (ROOT/'build/vendor').glob('*.v')},
        'files': {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in sorted(files)}})
    print(f'CP9 recorded: {len(archives)} synthesis archives; {report_count} raw report hashes; '
          '59119 completed DCJ11 cases per memory/ROM combination; 430 benchmark runs per ROM.')


if __name__ == '__main__': main()
