#!/usr/bin/env python3
"""Archive CP27 after exact FIS parity, legacy regression and fit-source checks."""
import csv
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile

from verify_cp26 import SUITES

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'build'
REPORTS = ROOT/'tb/reports'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read(name):
    return json.loads((ROOT/name).read_text())


def rows(data):
    return list(csv.DictReader(io.StringIO(data.decode())))


def save(name, data):
    (ROOT/name).write_text(json.dumps(data, indent=2, sort_keys=True)+'\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fram-tag', default='fis-iverilog-vendor-2')
    args = p.parse_args()
    vendor_tags = {-1: 'fis-iverilog-vendor--1', 2: args.fram_tag}
    # Refuse an incomplete run before creating any CP27 archive files.
    for mode in (-1, 2):
        for tag in (f'fis-verilator-portable-{mode}', vendor_tags[mode]):
            log = BUILD/f'{tag}.log'
            if not log.exists() or ': 23840 exact' not in log.read_text():
                raise SystemExit(f'CP27 not ready: complete FIS run required in {log}')
    baseline = read('docs/verification-cp26.json')
    stats = read('microcode/generated/m0.stats.json')
    assert (stats['used_words'], stats['encoding_version'], stats['word_bits']) == (954, 12, 36)
    assert not list((ROOT/'rtl').glob('*mmu*'))
    changed = {'rtl/uj11_datapath.v', 'rtl/uj11_decode.v', 'microasm/uj11asm.py',
               'tb/tb_datapath.v', 'tb/tb_decode.v', 'tb/test_microasm.py',
               'tb/trace_bit_vectors.c', 'tb/tb_system_flags_bench.v'}
    unchanged = []
    for name, expected in baseline['files'].items():
        if name.startswith(('rtl/', 'microcode/', 'microasm/', 'reference/lsi11/', 'tb/', '../core/', '../tests/')):
            if not name.startswith(('tb/reports/', 'microcode/generated/')) and name not in changed:
                assert digest((ROOT/name).read_bytes()) == expected, 'unintended change: '+name
                unchanged.append(name)
    assert {str(p.relative_to(ROOT)) for p in (ROOT/'rtl').glob('*.v')} == {
        n for n in baseline['files'] if n.startswith('rtl/') and n.endswith('.v')}
    sys.path.insert(0, str(ROOT/'microasm'))
    from uj11asm import assemble
    gold, listing, oldlabels, _ = assemble((ROOT/'microcode/m0.uasm').read_text())
    gate, _, labels, _ = assemble((ROOT/'microcode/generated/full.uasm').read_text())
    used = [int(line.split()[0], 16) for line in listing.splitlines()]
    assert len(used) == 700 and len(oldlabels) == 271
    assert all(gold[a] == gate[a] for a in used)
    assert all(labels[n] == a for n, a in oldlabels.items())
    assert labels['FIS_ENTRY'] == 0x11
    selected = {}
    for name, expected in [('cp27a', (863, 299, 4)), ('cp27b', (1095, 416, 4))]:
        folder = ROOT/'synth/reports'/name
        inputs = read(f'synth/reports/{name}/inputs.json')
        result = read(f'synth/reports/{name}/result.json')
        assert result['inputs'] == inputs
        assert result['timing_pass'] and result['fully_routed'] and result['diamond_returncode'] == 0
        assert tuple(result[k] for k in ('lut4', 'ff', 'ebr')) == expected
        with tarfile.open(folder/'source.tgz') as archive:
            for path, sha in inputs['files'].items():
                if path.startswith('generated:'):
                    assert digest((folder/path.split(':')[1]).read_bytes()) == sha
                else:
                    assert digest(archive.extractfile(path).read()) == sha
                    assert digest((ROOT/path).read_bytes()) == sha, 'fit input changed: '+path
        for extension, sha in result['reports'].items():
            assert digest((folder/('design'+extension)).read_bytes()) == sha
        selected[name] = {k: result[k] for k in ('lut4', 'ff', 'ebr', 'constraint_mhz', 'fmax_mhz', 'scope', 'device')}
    models = {p.name: digest(p.read_bytes()) for p in (BUILD/'vendor').glob('*.v')}
    assert models == baseline['vendor_models_sha256']
    logs = ['cp27-integer-regression.log', 'cp27-core-python.log', 'cp27-decode.log',
            'cp27-fis-negative.log', 'cp27-fis-bench-portable.log', 'cp27-fis-bench-vendor.log']
    text = (BUILD/'cp27-integer-regression.log').read_text()
    integer = {}
    normal, faults = 12928, 0
    for suite in SUITES:
        integer[suite] = {}
        for mode, label in [(-1, 'ram'), (2, 'fram')]:
            data = (BUILD/f'{suite}-cycles-{mode}.csv').read_bytes()
            olddata = gzip.decompress((REPORTS/f'cp26-{suite}-cycles-{label}.csv.gz').read_bytes())
            current, previous = rows(data), rows(olddata)
            if suite == 'illegal':
                previous = [r for r in previous if int(r['opcode'], 8) & 0o177740 != 0o075000]
                for i, r in enumerate(previous):
                    r['case'] = str(i)
                assert len(current) == 4096
            elif suite == 'trace_bit':
                changed_records = 0
                for r in previous:
                    if r['opcode'] == '075000':
                        r['opcode'] = '075040'
                        changed_records += 1
                assert changed_records == 64
            else:
                assert data == olddata, suite+' byte parity'
            assert current == previous, suite+' normalized parity'
            assert [int(r['case']) for r in current] == list(range(len(current)))
            display = {'bus-fault': 'bus fault', 'psw-transfer-fault': 'PSW-transfer fault',
                       'eis-ash-fault': 'ASH fault', 'eis-ashc-fault': 'ASHC fault',
                       'eis-xor-fault': 'XOR fault', 'eis-mul-fault': 'MUL fault',
                       'eis-div-fault': 'DIV fault'}.get(suite, suite)
            assert f'PASS {display} differential mode{mode}: {len(current)}' in text
            integer[suite][label] = dict(cases=len(current), microclocks=sum(int(r['microclocks']) for r in current),
                                         memory_beats=sum(int(r['memory_beats']) for r in current),
                                         cp26_byte_equal=data == olddata, cp26_equal_after_documented_probe_update=True)
            (REPORTS/f'cp27-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
        if 'fault' in suite:
            faults += len(current)
        else:
            normal += len(current)
        fixture = (BUILD/f'{suite}-vectors.txt').read_bytes()
        if suite not in ('illegal', 'trace_bit'):
            assert digest(fixture) == baseline['fixtures_uncompressed_sha256'][suite]
        (REPORTS/f'cp27-{suite}-vectors.txt.gz').write_bytes(gzip.compress(fixture, mtime=0))
    assert (normal, faults) == (268843, 49596)
    for marker in ('PASS differential: 12928', 'PASS FRAM differential: 12928',
                   'PASS lsi11 peripherals: 19', 'PASS prefetch: 525',
                   'PASS fault IRQ oracle: 8', 'exactly 59024 supported encodings'):
        assert marker in text, marker
    assert 'All core instruction tests passed' in (BUILD/'cp27-core-python.log').read_text()
    assert 'Ran 21 tests' in (BUILD/'cp27-core-python.log').read_text()
    assert 'all 65536 checked; only FIS differ' in (BUILD/'cp27-decode.log').read_text()
    negatives = read('build/cp27-fis-negative.json')
    assert negatives['positive_cases'] == 5616 and len(negatives['results']) == 15
    assert negatives['production_microcode_sha256'] == digest((ROOT/'microcode/fis.uasm').read_bytes())
    assert negatives['fixture_sha256'] == digest((BUILD/'fis-negative/vectors.txt').read_bytes())
    for item in negatives['results']:
        data = (BUILD/f'fis-negative/{item["name"]}.log').read_bytes()
        assert item['rejected'] and digest(data) == item['log_sha256'] and b'FIS case' in data
    proof = read('build/cp27-datapath-equivalence.json')
    assert proof['candidate_sha256'] == digest((ROOT/'rtl/uj11_datapath.v').read_bytes())
    assert proof['baseline_sha256'] == read('synth/reports/cp26a/inputs.json')['files']['rtl/uj11_datapath.v']
    for kind, result in proof['results'].items():
        data = (BUILD/f'cp27-datapath-{kind}.log').read_bytes()
        assert digest(data) == result['log_sha256']
        assert (result['returncode'] == 0) == (kind == 'positive')
    fis = {}
    for mode, label in [(-1, 'ram'), (2, 'fram')]:
        portable = f'fis-verilator-portable-{mode}'
        vendor = vendor_tags[mode]
        data = (BUILD/f'{portable}.csv').read_bytes()
        assert data == (BUILD/f'{vendor}.csv').read_bytes(), 'FIS ROM parity'
        values = rows(data)
        assert len(values) == 23840 and [int(r['case']) for r in values] == list(range(23840))
        fis[label] = dict(cases=len(values), portable_vendor_identical=True,
                          microclocks=sum(int(r['microclocks']) for r in values),
                          memory_beats=sum(int(r['memory_beats']) for r in values))
        for name in (portable, vendor):
            assert ': 23840 exact' in (BUILD/f'{name}.log').read_text()
            logs.extend([f'{name}.log', f'{name}-build.log'])
            shard_folder = BUILD/f'{name}-shards'
            if shard_folder.exists():
                with tarfile.open(REPORTS/f'cp27-{name}-shards.tgz', 'w:gz') as archive:
                    for path in sorted(shard_folder.iterdir()):
                        if path.is_file():
                            archive.add(path, arcname=path.name)
        (REPORTS/f'cp27-fis-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
    (REPORTS/'cp27-fis-vectors.txt.gz').write_bytes(gzip.compress((BUILD/'fis-vectors.txt').read_bytes(), mtime=0))
    benchmarks = {}
    for mode in (-1, 1, 2):
        data = (BUILD/f'fis-bench-portable-{mode}.json').read_bytes()
        assert data == (BUILD/f'fis-bench-vendor-{mode}.json').read_bytes(), 'FIS benchmark parity'
        values = json.loads(data)
        assert len(values) == 8
        for r in values:
            assert (r['instructions'], r['fis_instructions'], r['memory_beats']) == (160, 32, 576)
            assert r['microclocks'] > r['fis_retire_clocks'] > 0
        benchmarks[str(mode)] = [dict(**r, loop_cpi=r['microclocks']/160,
                                     fis_retire_interval=r['fis_retire_clocks']/32,
                                     calculated_fis_per_sec_including_loop_at_29_56_mhz=29560000*32/r['microclocks']) for r in values]
        (REPORTS/f'cp27-fis-benchmarks-{mode}.json.gz').write_bytes(gzip.compress(data, mtime=0))
        for kind in ('portable', 'vendor'):
            logs.extend([f'fis-bench-{kind}-{mode}.log', f'fis-bench-{kind}-{mode}-build.log'])
    for name in logs:
        data = (BUILD/name).read_text()
        assert data and not any(s in data for s in ('FATAL', 'FAILED', 'Traceback', '%Error', '%Warning', 'ERROR:')), name
        shutil.copyfile(BUILD/name, REPORTS/name)
    for name in ('cp27-fis-negative.json', 'cp27-datapath-equivalence.json',
                 'cp27-datapath-positive.log', 'cp27-datapath-negative.log'):
        shutil.copyfile(BUILD/name, REPORTS/name)
    with tarfile.open(REPORTS/'cp27-fis-negative.tgz', 'w:gz') as a:
        for p in sorted((BUILD/'fis-negative').iterdir()):
            if p.is_file():
                a.add(p, arcname=p.name)
    save('docs/benchmarks-cp27.json', dict(clock_hz_nominal=29560000, spi_divider=1, method='Functional RTL simulation, 1 warmup loop and 32 measured MOV/MOV/MOV/FIS/BR loops; no board measurement.',
         fis_runs_each_rom_model=24, fis_benchmarks=benchmarks, fis_differential=fis, integer_portable_cycles=integer,
         old_vendor_and_1146_benchmarks='Historical CP26 evidence, not rerun as CP27; old datapath formally equivalent and old microcode preserved.'))
    source = set()
    for pattern in ('docs/*.md', 'rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*', 'microasm/*.py',
                    'tb/*.v', 'tb/*.c', 'tb/*.h', 'tb/test_*.py', 'tools/*.py', 'tools/formal-requirements.txt',
                    'reference/lsi11/*', 'synth/machxo2/*.v', 'synth/machxo2/*.lpf', 'synth/machxo2/*.sty'):
        source.update(p for p in ROOT.glob(pattern) if p.is_file())
    source.update(ROOT/n for n in ('README.md', 'Makefile', '../core/core.c', '../core/core.h', '../core/hardware.c',
                                  '../core/hardware.h', '../core/pdp11_fp.c', '../core/disas.c', '../core/disas.h', '../tests/core_tests.c'))
    with tarfile.open(REPORTS/'cp27-verification-source.tgz', 'w:gz') as a:
        for p in sorted(source):
            name = str(p.relative_to(ROOT))
            a.add(p, arcname=name[3:] if name.startswith('../') else 'uJ11-fpga/'+name)
    files = source | set(REPORTS.glob('cp27*')) | {ROOT/'docs/benchmarks-cp27.json'}
    for name in selected:
        files.update((ROOT/'synth/reports'/name).iterdir())
    save('docs/verification-cp27.json', dict(date='2026-09-09', baseline='CP26',
         profile='Kernel-only integer + FIS exact rounded F-format; no MMU; not cycle/guard-bit emulation of KE11-F.',
         encoding_version=12, microcode_words=954, unchanged_words=700, unchanged_labels=271, labels=len(labels),
         checks=dict(integer_cases_each_portable_memory=normal, integer_fault_cases_each_portable_memory=faults,
                     fis_cases_each_memory_rom=23840, fis_benchmarks_each_rom=24, python_tests=21,
                     decoder_encodings=65536, new_fis_encodings=32,
                     illegal_change='64 newly supported FIS records removed; remaining results identical after renumbering.',
                     trace_change='64 reserved-opcode probes moved from 075000 to 075040; cycle/beat counts unchanged.',
                     current_inputs_equal_synthesis=list(selected)),
         fis_vendor_run_tags=vendor_tags,
         synthesis=selected, fis_negative_controls=negatives, datapath_equivalence=proof,
         baseline_manifest_sha256=digest((ROOT/'docs/verification-cp26.json').read_bytes()),
         unchanged_baseline_sources=unchanged, vendor_models_sha256=models,
         limits=['Full UART/timer/panel/SD/RK and external pin timing are not included in fit.',
                 'No FPGA programming or RT-11 boot. Full FP11 not implemented; 70 control-store words free.',
                 'Fresh integer regression uses portable ROM; previous full vendor integer/benchmark results remain CP26 evidence.',
                 'FIS arithmetic uses Fraction oracle, not host-float C core or KE11-F diagnostic certification.'],
         files={str(p.relative_to(ROOT)): digest(p.read_bytes()) for p in sorted(files) if p.is_file()}))
    print('CP27 recorded: 23840 FIS cases per RAM/FRAM x portable/vendor; 24 benchmarks/ROM; '
          '268843 integer + 49596 faults per portable memory; 15 mutations rejected; formal proof and both fits verified.')


if __name__ == '__main__':
    main()
