#!/usr/bin/env python3
"""Record CP12 only after exact portable/vendor and historical regression checks."""
import csv
import gzip
import io
from pathlib import Path
import shutil
import tarfile

from record_ea import ROOT, MODES, compare_results, read_json, require, sha256, write_json

FINAL = ['cp12c', 'cp12d']
LOGS = ['cp12-tests.log', 'cp12-vendor-base.log', 'cp12-vendor-isa.log',
        'cp12-benchmarks-portable.log', 'cp12-benchmarks-vendor.log']
COUNTS = {'ea': (31671, 137006), 'single': (7080, 19588), 'branch': (7440, 7440),
          'byte': (23954, 98606), 'single_byte': (7104, 19680), 'control': (14814, 30956), 'extra': (4524, 9762)}


def main():
    reports = ROOT / 'tb/reports'
    stats = read_json(ROOT / 'microcode/generated/m0.stats.json')
    require((stats['encoding_version'], stats['used_words'], stats['word_bits']) == (7, 393, 36),
            'Microcode format/occupancy changed')
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
                require(f'PASS {suite} differential mode{mode}: {count}' in logs[name],
                        f'{name}: missing {suite}/{mode}')
        for marker in ['PASS EA directed: 14', 'PASS single directed: 46',
                       'PASS byte I/O: 23', 'PASS single byte directed: 46', 'PASS control directed: 58', 'PASS extra directed: 36']:
            require(marker in logs[name], f'{name}: {marker}')
    for marker in ['Ran 12 tests', '4096 pair checks', 'PASS prefetch: 525',
                   'PASS lsi11 peripherals: 19', 'exactly 55744 supported encodings',
                   'PASS byte ALU: 2097152', 'PASS byte datapath: 16384']:
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
                    if folder.name in FINAL:
                        require(sha256((ROOT / path).read_bytes()) == digest,
                                f'{folder.name}: current synthesis input changed: {path}')
                require(sha256(data) == digest, f'{folder.name}: archive mismatch: {path}')
        if 'placement' in info:
            require(sha256((folder / 'design.ncd').read_bytes()) == info['placement']['ncd_sha256'],
                    f'{folder.name}: NCD mismatch')
        if (folder / 'result.json').exists():
            result = read_json(folder / 'result.json')
            for key, digest in result.get('reports', {}).items():
                require(sha256((folder / ('design' + (Path(key).suffix or key))).read_bytes()) == digest,
                        f'{folder.name}: raw report mismatch: {key}')
                report_count += 1
            if folder.name in FINAL:
                require(result['timing_pass'] and result['ebr'] == 4, f'{folder.name}: gate failed')
        archives.append(folder.name)

    # Existing routines and their entry addresses must remain bit-identical.
    import importlib.util
    spec = importlib.util.spec_from_file_location('cp12_asm', ROOT/'microasm/uj11asm.py')
    asm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(asm)
    with tarfile.open(ROOT/'synth/reports/cp11f/source.tgz') as archive:
        old_source = archive.extractfile('microcode/m0.uasm').read().decode()
    old_image, old_listing, old_labels, _ = asm.assemble(old_source)
    new_image, _, new_labels, _ = asm.assemble((ROOT/'microcode/m0.uasm').read_text())
    old_addresses = [int(line.split()[0], 16) for line in old_listing.splitlines()]
    require(len(old_addresses) == 355, 'CP11 occupied words')
    require(all(old_image[a] == new_image[a] for a in old_addresses), 'CP11 microcode changed')
    require(all(new_labels[k] == v for k, v in old_labels.items()), 'CP11 label moved')

    cycles, fixtures = {}, {}
    for suite, (count, beats) in COUNTS.items():
        cycles[suite] = {}
        for mode, label in [(-1, 'ram'), (2, 'fram')]:
            data = (ROOT / 'build' / f'{suite}-cycles-{mode}.csv').read_bytes()
            require(data == (ROOT / 'build' / f'{suite}-cycles-{mode}-portable.csv').read_bytes(),
                    f'{suite}/{label}: per-case portable/vendor mismatch')
            rows = list(csv.DictReader(io.StringIO(data.decode())))
            require(len(rows) == count, f'{suite}: case count')
            require(sum(int(r['memory_beats']) for r in rows) == beats, f'{suite}: bus count')
            if suite != 'extra':
                require(data == gzip.decompress((reports / f'cp11-{suite}-cycles-{label}.csv.gz').read_bytes()),
                        f'CP11 {suite} per-case regression')
            (reports / f'cp12-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
            cycles[suite][label] = dict(cases=count, microclocks=sum(int(r['microclocks']) for r in rows),
                                       memory_beats=beats, portable_vendor_equal=True)
        data = (ROOT / 'build' / f'{suite}-vectors.txt').read_bytes()
        if suite != 'extra':
            require(data == gzip.decompress((reports / f'cp11-{suite}-vectors.txt.gz').read_bytes()),
                    f'CP11 {suite} fixture changed')
        (reports / f'cp12-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data, mtime=0))
        fixtures[suite] = sha256(data)
        shutil.copyfile(ROOT / 'build' / f'{suite}-oracle.log', reports / f'cp12-{suite}-oracle.log')
    require('23954 completed cases; 516 excluded' in (reports/'cp12-byte-oracle.log').read_text(),
            'Byte oracle exclusions changed')
    require('abort=470 trap=0 I/O=60 odd-word=0' in (reports/'cp12-byte-oracle.log').read_text(),
            'Byte oracle exclusion reasons changed')

    extra_log = (reports/'cp12-extra-oracle.log').read_text()
    for marker in ['SWAB/SXT: 1468 completed', 'MARK: 3056 completed',
                   '4524 completed cases; 1044 excluded',
                   'abort=4 trap=4 I/O=1040 odd-word=0']:
        require(marker in extra_log, f'Extra oracle: {marker}')

    cp8 = read_json(ROOT / 'docs/benchmarks-cp8.json')
    cp9 = read_json(ROOT / 'docs/benchmarks-cp9.json')
    cp10 = read_json(ROOT / 'docs/benchmarks-cp10.json')
    require(compare_results('benchmarks', 27) == cp8['rr_ram']['results'], 'CP9 RR RAM regression')
    rr_fram = [dict(memory_mode=m, **r) for m in range(3)
               for r in compare_results(f'fram-benchmarks-{m}', 9)]
    require(rr_fram == cp8['rr_fram']['results'], 'CP9 RR FRAM regression')
    groups = {}
    for stem, count in [('ea', 25), ('cp9', 69), ('byte', 20), ('single-byte', 24), ('control', 8), ('extra', 7)]:
        rows = []
        for mode, label in MODES.items():
            for row in compare_results(f'{stem}-benchmarks-{mode}', count):
                cpi = row['microclocks'] / row['instructions']
                rows.append(dict(memory_mode=mode, memory_model=label, **row,
                    microclocks_per_instruction=cpi,
                    memory_beats_per_instruction=row['memory_beats']/row['instructions'],
                    calculated_ips_at_29_56_mhz=29560000/cpi))
        groups[stem] = rows
    require(groups['ea'] == cp8['ea_loop']['results'], 'CP9 EA benchmark regression')
    require(groups['cp9'] == cp9['results'], 'CP9 unary/branch benchmark regression')
    require(groups['byte'] == cp10['double_byte'], 'CP10 byte benchmarks changed')
    require(groups['single-byte'] == cp10['single_byte'], 'CP10 unary-byte benchmarks changed')
    cp11 = read_json(ROOT / 'docs/benchmarks-cp11.json')
    require(groups['control'] == cp11['control_flow'], 'CP11 control benchmark regression')
    write_json(ROOT / 'docs/benchmarks-cp12.json', {
        'date': '2026-09-09', 'clock_hz_nominal': 29560000, 'spi_divider': 1,
        'method': 'Functional RTL simulation; portable/vendor DP8KC counts identical; no board measurement.',
        'runs_each_rom_model': 666, 'cp11_all_638_benchmark_runs_unchanged': True,
        'extra': groups['extra'], 'differential_cycles': cycles})
    for name in LOGS: shutil.copyfile(ROOT / 'build' / name, reports / name)
    data = (ROOT / 'build/isa_vectors.mem').read_bytes()
    require(data == gzip.decompress((reports / 'cp8-rr-vectors.mem.gz').read_bytes()), 'CP9 RR fixture changed')
    fixtures['rr'] = sha256(data)
    original = (ROOT / '../core/core.c').read_bytes()
    audited = (ROOT / 'build/oracle_core.c').read_bytes()
    # The hook generator verifies the inverse transformation. Re-run it in
    # memory here using its public transform, so audit provenance is explicit.
    from oracle_core import instrument
    require(audited == instrument(original.decode()).encode(), 'Oracle instrumentation changed')
    (reports / 'cp12-oracle_core.c.gz').write_bytes(gzip.compress(audited, mtime=0))
    control_audit = (ROOT/'build/oracle_control_core.c').read_bytes()
    require(control_audit == instrument(original.decode(), vectors=True).encode(), 'Control oracle audit changed')
    (reports/'cp12-oracle_control_core.c.gz').write_bytes(gzip.compress(control_audit, mtime=0))
    files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*.v', 'microcode/generated/*.mem',
                    'microasm/*.py', 'tb/*.v', 'tb/*.c', 'tb/*.h', 'tb/test_*.py', 'reference/lsi11/*.v',
                    'tools/*.py', 'tb/reports/cp12*', 'tb/reports/cp12*/*', 'synth/reports/cp12*/inputs.json',
                    'synth/reports/cp12*/result.json']:
        files.update(p for p in ROOT.glob(pattern) if p.is_file())
    files.update(ROOT/p for p in ['Makefile', '../core/core.c', '../core/core.h', '../core/hardware.c',
        '../core/hardware.h', '../core/pdp11_fp.c', 'docs/benchmarks-cp12.json'])
    write_json(ROOT / 'docs/verification-cp12.json', {
        'date': '2026-09-09', 'encoding_version': 7, 'microcode_words': 393,
        'isa': 'CP11 word/byte/control ISA plus SWAB/SXT/MARK; no MMU.',
        'checks': {'python_test_methods': 12, 'byte_alu_checks': 2097152, 'byte_writeback_checks': 16384,
            'completed_dcj11_cases_each_memory_rom_pair': 109515, 'dcj11_rr_br_cases': 12928,
            'dcj11_byte_candidates': 24470, 'dcj11_byte_completed': 23954,
            'dcj11_byte_excluded': {'total': 516, 'abort': 470, 'io': 60, 'abort_io_overlap': 14},
            'dcj11_single_byte_cases': 7104, 'byte_io_directed_cases': 23, 'single_byte_directed_cases': 46,
            'decoder_encodings_checked': 65536, 'decoder_encodings_supported': 55744,
            'prefetch_protocol_beats': 525, 'legacy_peripheral_beats': 19,
            'benchmark_runs_each_rom_model': 666, 'cp11_benchmarks_unchanged': True,
            'extra_directed_cases': 36, 'dcj11_extra_cases': 4524,
            'dcj11_extra_excluded': {'total': 1044, 'abort': 4, 'trap': 4, 'io': 1040},
            'control_directed_cases': 58, 'dcj11_control_cases': 14814,
            'dcj11_control_excluded': {'total': 456, 'trap': 448, 'io': 402, 'trap_io_overlap': 394},
            'differential_cycles': cycles, 'synthesis_archives_verified': archives,
            'cp11_all_355_microcode_words_and_labels_unchanged': True,
            'raw_report_hashes_verified': report_count, 'current_inputs_equal_synthesis': FINAL},
        'oracle': {'original_core_sha256': sha256(original), 'audited_core_sha256': sha256(audited),
            'control_audited_core_sha256': sha256(control_audit),
            'audit': 'Four address hooks; control build also has one vector-entry hook. Source emulator unchanged.'},
        'limits': ['No architectural traps/interrupts, PSW ISA, EIS or J-11 banking.',
            'Abort partial-register state and internal DCJ11 CSRs are outside this differential claim.',
            'No board programming, full peripheral synthesis top or external pin timing closure.'],
        'fixtures_uncompressed_sha256': fixtures,
        'vendor_models_sha256': {p.name: sha256(p.read_bytes()) for p in (ROOT/'build/vendor').glob('*.v')},
        'files': {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in sorted(files)}})
    print(f'CP12 recorded: {len(archives)} synthesis archives; {report_count} raw report hashes; '
          '109515 completed DCJ11 cases per memory/ROM pair; 666 benchmark runs per ROM.')


if __name__ == '__main__': main()
