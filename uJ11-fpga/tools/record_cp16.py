#!/usr/bin/env python3
"""Record CP16 only after exact portable/vendor and historical regression checks."""
import csv
import gzip
import io
from pathlib import Path
import shutil
import tarfile

from record_ea import ROOT, MODES, compare_results, read_json, require, sha256, write_json

FINAL = ['cp16e', 'cp16f']
LOGS = ['cp16-tests.log', 'cp16-vendor-base.log', 'cp16-vendor-isa.log',
        'cp16-benchmarks-portable.log', 'cp16-benchmarks-vendor.log', 'cp16-decode-equivalence.log', 'cp16-bus-fault.log', 'cp16-fault-system.log', 'cp16-fault-double.log', 'cp16-vendor-bus-fault.log', 'cp16-fault-irq-oracle.log']
COUNTS = {'ea': (31671, 137006), 'single': (7080, 19588), 'branch': (7440, 7440),
          'byte': (23954, 98606), 'single_byte': (7104, 19680), 'control': (14814, 30956), 'extra': (4524, 9762), 'trap': (4640, 21088), 'irq': (6654, 18678), 'illegal': (4160, 20800)}


def main():
    reports = ROOT / 'tb/reports'
    stats = read_json(ROOT / 'microcode/generated/m0.stats.json')
    require((stats['encoding_version'], stats['used_words'], stats['word_bits']) == (9, 452, 36),
            'Microcode format/occupancy changed')
    log_names=LOGS
    logs = {n: (ROOT / 'build' / n).read_text() for n in log_names}
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
                       'PASS byte I/O: 23', 'PASS single byte directed: 46', 'PASS control directed: 58', 'PASS extra directed: 36', 'PASS trap directed: 112', 'PASS IRQ directed: 104', 'PASS legacy IRQ: 5 scenarios, 6 settled-state checks', 'PASS illegal directed: 96']:
            require(marker in logs[name], f'{name}: {marker}')
    for marker in ['Ran 14 tests', '4096 pair checks', 'PASS prefetch: 525',
                   'PASS lsi11 peripherals: 19', 'exactly 56268 supported encodings',
                   'PASS byte ALU: 2097152', 'PASS byte datapath: 16384', 'PASS IRQ adapter: 763']:
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
    spec = importlib.util.spec_from_file_location('cp16_asm', ROOT/'microasm/uj11asm.py')
    asm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(asm)
    with tarfile.open(ROOT/'synth/reports/cp15f/source.tgz') as archive:
        old_source = archive.extractfile('microcode/m0.uasm').read().decode()
    old_image, old_listing, old_labels, _ = asm.assemble(old_source)
    new_image, _, new_labels, _ = asm.assemble((ROOT/'microcode/m0.uasm').read_text())
    old_addresses = [int(line.split()[0], 16) for line in old_listing.splitlines()]
    require(len(old_addresses) == 450, 'CP15 occupied words')
    changed_words=[]
    for a in old_addresses:
        expected=old_image[a]
        if expected>>35 and expected>>31&15==0 and expected>>11&1023==old_labels['TRAP_ENTRY']:
            expected |= 15<<31
        elif a in (0x224,0x230,0x2a8):
            expected |= 4
        require(new_image[a]==expected, f'Unexpected CP15 microcode change at {a:03x}')
        if old_image[a]!=expected:changed_words.append(f'{a:03x}')
    require(len(changed_words)==10, 'Expected seven TRAP commands and three fault_inc flags')
    require(all(new_labels[k] == v for k, v in old_labels.items()), 'CP15 label moved')

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
            require(data == gzip.decompress((reports / f'cp15-{suite}-cycles-{label}.csv.gz').read_bytes()),
                    f'CP15 {suite} per-case regression')
            (reports / f'cp16-{suite}-cycles-{label}.csv.gz').write_bytes(gzip.compress(data, mtime=0))
            cycles[suite][label] = dict(cases=count, microclocks=sum(int(r['microclocks']) for r in rows),
                                       memory_beats=beats, portable_vendor_equal=True)
        data = (ROOT / 'build' / f'{suite}-vectors.txt').read_bytes()
        require(data == gzip.decompress((reports / f'cp15-{suite}-vectors.txt.gz').read_bytes()),
                f'CP15 {suite} fixture changed')
        (reports / f'cp16-{suite}-vectors.txt.gz').write_bytes(gzip.compress(data, mtime=0))
        fixtures[suite] = sha256(data)
        shutil.copyfile(ROOT / 'build' / f'{suite}-oracle.log', reports / f'cp16-{suite}-oracle.log')
    require('23954 completed cases; 516 excluded' in (reports/'cp16-byte-oracle.log').read_text(),
            'Byte oracle exclusions changed')
    require('abort=470 trap=0 I/O=60 odd-word=0' in (reports/'cp16-byte-oracle.log').read_text(),
            'Byte oracle exclusion reasons changed')

    extra_log = (reports/'cp16-extra-oracle.log').read_text()
    for marker in ['SWAB/SXT: 1468 completed', 'MARK: 3056 completed',
                   '4524 completed cases; 1044 excluded',
                   'abort=4 trap=4 I/O=1040 odd-word=0']:
        require(marker in extra_log, f'Extra oracle: {marker}')

    trap_log = (reports/'cp16-trap-oracle.log').read_text()
    for marker in ['Software traps: 3584 completed', 'RTI: 1056 completed',
                   '4640 completed cases; 0 excluded',
                   'abort=0 trap/vector=0 I/O=0 odd-word=0']:
        require(marker in trap_log, f'Trap oracle: {marker}')

    cp8 = read_json(ROOT / 'docs/benchmarks-cp8.json')
    cp9 = read_json(ROOT / 'docs/benchmarks-cp9.json')
    cp10 = read_json(ROOT / 'docs/benchmarks-cp10.json')
    require(compare_results('benchmarks', 27) == cp8['rr_ram']['results'], 'CP9 RR RAM regression')
    rr_fram = [dict(memory_mode=m, **r) for m in range(3)
               for r in compare_results(f'fram-benchmarks-{m}', 9)]
    require(rr_fram == cp8['rr_fram']['results'], 'CP9 RR FRAM regression')
    groups = {}
    for stem, count in [('ea', 25), ('cp9', 69), ('byte', 20), ('single-byte', 24), ('control', 8), ('extra', 7), ('trap', 6), ('irq', 6), ('illegal', 3)]:
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
    cp12 = read_json(ROOT / 'docs/benchmarks-cp12.json')
    require(groups['extra'] == cp12['extra'], 'CP12 extra benchmarks changed')
    cp13 = read_json(ROOT / 'docs/benchmarks-cp13.json')
    require(groups['trap'] == cp13['trap'], 'CP13 trap benchmarks changed')
    irq_log = (reports/'cp16-irq-oracle.log').read_text()
    require('6654 completed; 0 excluded' in irq_log and 'abort=0 vector=0 I/O=0 odd=0' in irq_log, 'IRQ oracle exclusions')
    cp14 = read_json(ROOT/'docs/benchmarks-cp14.json')
    require(groups['irq'] == cp14['irq'], 'CP14 IRQ benchmarks changed')
    illegal_log = (reports/'cp16-illegal-oracle.log').read_text()
    for marker in ['4160 completed of 18536 candidates; 14376 excluded',
                   'abort=394 different/no-vector=14376 I/O=550 odd-word=0']:
        require(marker in illegal_log, 'Illegal oracle exclusions: '+marker)
    require('PASS decoder equivalence: all 65536 opcodes match archived CP15a' in logs[LOGS[5]],
            'Decoder refactoring equivalence missing')
    candidate_data=(ROOT/'build/illegal-opcodes.txt').read_bytes()
    require(len(candidate_data.splitlines())==9268, 'Unsupported candidate opcode count')
    (reports/'cp16-illegal-opcodes.txt.gz').write_bytes(gzip.compress(candidate_data,mtime=0))
    headers=[line.split() for line in (ROOT/'build/illegal-vectors.txt').read_text().splitlines() if len(line.split())==12]
    require(len(headers)==4160 and len({h[1] for h in headers})==2080, 'Illegal fixture headers')
    require(sum((int(h[1],16)&0o177770)==0o000100 for h in headers)==16, 'JMP vector004 coverage')
    require(groups['illegal']==read_json(ROOT/'docs/benchmarks-cp15.json')['illegal'], 'CP15 illegal benchmarks changed')
    negative=(ROOT/'build/cp16-no-repair-negative.log').read_bytes()
    require(b' R0 got2000 expected2002' in negative, 'Missing no-repair negative control')
    (reports/'cp16-no-repair-negative.log').write_bytes(negative)
    fault_cycles={}
    for mode,label in [(-1,'ram'),(2,'fram')]:
        data=(ROOT/'build'/f'bus-fault-cycles-{mode}.csv').read_bytes()
        require(data==(ROOT/'build'/f'bus-fault-cycles-{mode}-portable.csv').read_bytes(), 'Fault portable/vendor cycles differ')
        rows=list(csv.DictReader(io.StringIO(data.decode())))
        require(len(rows)==32780 and [int(r['case']) for r in rows]==list(range(32780)), 'Fault cases/IDs')
        require(sum(int(r['repair_clocks']) for r in rows)==3536, 'Fault repair count')
        require(sum(int(r['memory_beats']) for r in rows)==213080 and sum(int(r['microclocks']) for r in rows)==(1179328 if mode==-1 else 20896206), 'Fault cycle/beat baseline')
        fault_cycles[label]=dict(cases=len(rows),microclocks=sum(int(r['microclocks']) for r in rows),
            memory_beats=sum(int(r['memory_beats']) for r in rows),repair_clocks=3536,portable_vendor_equal=True)
        (reports/f'cp16-bus-fault-cycles-{label}.csv.gz').write_bytes(gzip.compress(data,mtime=0))
        marker=f'PASS bus fault differential mode{mode}: 32780'
        require(marker in logs['cp16-bus-fault.log'], marker)
        require(marker in logs['cp16-vendor-bus-fault.log'], marker)
    for marker,log in [('PASS bus fault system: 32','cp16-fault-system.log'),('PASS bus double fault: 96','cp16-fault-double.log')]:
        require(marker in logs[log] and marker in logs['cp16-vendor-bus-fault.log'], marker)
    require('PASS fault IRQ oracle: 8' in logs['cp16-fault-irq-oracle.log'], 'Missing primary-core IRQ order test')
    irq_negative=(ROOT/'build/cp16-early-irq-negative.log').read_bytes()
    require(b'IRQ accepted before the expected handler instruction' in irq_negative, 'Missing early-IRQ negative control')
    (reports/'cp16-early-irq-negative.log').write_bytes(irq_negative)
    fault_log=(ROOT/'build/bus-fault-oracle.log').read_text()
    for marker in ['32780 completed frames (28256 ACK error, 4524 odd-word), 8 excluded',
                   '296 changed register/PSW states, 4 additional bus traces']:
        require(marker in fault_log, 'Fault oracle: '+marker)
    exclusions=list(csv.DictReader((ROOT/'build/bus-fault-excluded.csv').open()))
    require(len(exclusions)==8 and all(r['io']=='1' for r in exclusions), 'Fault exclusions')
    continuation=list(csv.DictReader((ROOT/'build/bus-fault-continuation.csv').open()))
    require(len(continuation)==296, 'Post-abort continuation audit')
    for name in ['bus-fault-vectors.txt','bus-fault-excluded.csv','bus-fault-continuation.csv','bus-fault-oracle.log']:
        data=(ROOT/'build'/name).read_bytes()
        (reports/('cp16-'+name+'.gz')).write_bytes(gzip.compress(data,mtime=0))
    fixtures['bus_fault_frame'] = sha256((ROOT/'build/bus-fault-vectors.txt').read_bytes())
    write_json(ROOT / 'docs/benchmarks-cp16.json', {
        'date': '2026-09-09', 'clock_hz_nominal': 29560000, 'spi_divider': 1,
        'method': 'Functional RTL simulation; portable/vendor DP8KC counts identical; no board measurement.',
        'runs_each_rom_model': 726, 'cp15_all_726_benchmark_runs_unchanged': True,
        'fault_frame_cycles': fault_cycles, 'differential_cycles': cycles})
    for name in log_names: shutil.copyfile(ROOT / 'build' / name, reports / name)
    data = (ROOT / 'build/isa_vectors.mem').read_bytes()
    require(data == gzip.decompress((reports / 'cp8-rr-vectors.mem.gz').read_bytes()), 'CP9 RR fixture changed')
    fixtures['rr'] = sha256(data)
    original = (ROOT / '../core/core.c').read_bytes()
    audited = (ROOT / 'build/oracle_core.c').read_bytes()
    # The hook generator verifies the inverse transformation. Re-run it in
    # memory here using its public transform, so audit provenance is explicit.
    from oracle_core import instrument
    require(audited == instrument(original.decode()).encode(), 'Oracle instrumentation changed')
    (reports / 'cp16-oracle_core.c.gz').write_bytes(gzip.compress(audited, mtime=0))
    control_audit = (ROOT/'build/oracle_control_core.c').read_bytes()
    require(control_audit == instrument(original.decode(), vectors=True).encode(), 'Control oracle audit changed')
    (reports/'cp16-oracle_control_core.c.gz').write_bytes(gzip.compress(control_audit, mtime=0))
    from fault_oracle import instrument_faults
    fault_audit=(ROOT/'build/oracle_fault_core.c').read_bytes()
    require(fault_audit==instrument_faults(original.decode()).encode(), 'Fault oracle instrumentation')
    (reports/'cp16-oracle_fault_core.c.gz').write_bytes(gzip.compress(fault_audit,mtime=0))
    # Preserve the complete tested source, including verification added after the fit.
    # Hardware source.tgz archives still retain each exact earlier synthesis input.
    source_files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*', 'microasm/*.py',
                    'tb/*.v', 'tb/*.c', 'tb/*.h', 'tb/test_*.py', 'reference/lsi11/*',
                    'tools/*.py', 'synth/machxo2/*.v', 'synth/machxo2/*.lpf', 'synth/machxo2/*.sty']:
        source_files.update(p for p in ROOT.glob(pattern) if p.is_file())
    source_files.update(ROOT/p for p in ['Makefile', 'README.md', '../core/core.c', '../core/core.h',
        '../core/hardware.c', '../core/hardware.h', '../core/pdp11_fp.c'])
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w') as archive:
        for path in sorted(source_files):
            data = path.read_bytes()
            name = ('core/'+path.name) if path.parent == ROOT/'../core' else 'uJ11-fpga/'+str(path.relative_to(ROOT))
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
    (reports/'cp16-verification-source.tgz').write_bytes(gzip.compress(buffer.getvalue(), mtime=0))
    files = set()
    for pattern in ['rtl/*.v', 'microcode/*.uasm', 'microcode/generated/*.v', 'microcode/generated/*.mem',
                    'microasm/*.py', 'tb/*.v', 'tb/*.c', 'tb/*.h', 'tb/test_*.py', 'reference/lsi11/*.v',
                    'tools/*.py', 'tb/reports/cp16*', 'tb/reports/cp16*/*', 'synth/reports/cp16*/inputs.json',
                    'synth/reports/cp16*/result.json']:
        files.update(p for p in ROOT.glob(pattern) if p.is_file())
    files.update(ROOT/p for p in ['Makefile', '../core/core.c', '../core/core.h', '../core/hardware.c',
        '../core/hardware.h', '../core/pdp11_fp.c', 'docs/benchmarks-cp16.json', 'docs/irq-source-audit.json'])
    write_json(ROOT / 'docs/verification-cp16.json', {
        'date': '2026-09-09', 'encoding_version': 9, 'microcode_words': 452,
        'isa': 'CP15 ISA plus memory bus/address vector004, fault autoincrement repair and terminal frame-error guard; kernel bank0, T=0, no MMU.',
        'checks': {'python_test_methods': 14, 'byte_alu_checks': 2097152, 'byte_writeback_checks': 16384,
            'completed_dcj11_cases_each_memory_rom_pair': 124969, 'dcj11_rr_br_cases': 12928,
            'dcj11_byte_candidates': 24470, 'dcj11_byte_completed': 23954,
            'dcj11_byte_excluded': {'total': 516, 'abort': 470, 'io': 60, 'abort_io_overlap': 14},
            'dcj11_single_byte_cases': 7104, 'byte_io_directed_cases': 23, 'single_byte_directed_cases': 46,
            'decoder_encodings_checked': 65536, 'decoder_encodings_supported': 56268,
            'prefetch_protocol_beats': 525, 'legacy_peripheral_beats': 19,
            'benchmark_runs_each_rom_model': 726, 'cp15_benchmarks_unchanged': True,
            'illegal_directed_cases': 96, 'dcj11_illegal_cases': 4160, 'dcj11_illegal_opcode_values': 2080,
            'dcj11_illegal_candidates': 18536, 'dcj11_illegal_excluded': {'total': 14376, 'abort': 394, 'different_or_no_vector': 14376, 'io': 550},
            'decoder_baseline_equivalence_encodings': 65536,
            'irq_directed_cases': 104, 'dcj11_irq_cases': 6654, 'dcj11_irq_excluded': 0,
            'irq_adapter_checks': 763, 'legacy_irq_scenarios': 5, 'legacy_irq_settled_state_checks': 6,
            'trap_directed_cases': 112, 'dcj11_trap_cases': 4640, 'dcj11_trap_excluded': 0,
            'extra_directed_cases': 36, 'dcj11_extra_cases': 4524,
            'dcj11_extra_excluded': {'total': 1044, 'abort': 4, 'trap': 4, 'io': 1040},
            'control_directed_cases': 58, 'dcj11_control_cases': 14814,
            'dcj11_control_excluded': {'total': 456, 'trap': 448, 'io': 402, 'trap_io_overlap': 394},
            'differential_cycles': cycles, 'synthesis_archives_verified': archives,
            'cp15_labels_unchanged': True, 'changed_microcode_addresses': changed_words,
            'dcj11_bus_fault_frame_cases': 32780, 'asynchronous_irq_deferred_until_fault_handler_instruction': True, 'bus_fault_ack_cases': 28256, 'bus_fault_odd_word_cases': 4524,
            'bus_fault_frame_cycles': fault_cycles, 'bus_fault_excluded_io': 8,
            'bus_fault_system_cases': 32, 'fault_irq_order_oracle_cases': 8, 'bus_double_fault_cases': 96,
            'fault_vendor_fixture_segments': [32780],
            'raw_report_hashes_verified': report_count, 'current_inputs_equal_synthesis': FINAL},
        'oracle': {'original_core_sha256': sha256(original), 'audited_core_sha256': sha256(audited),
            'control_audited_core_sha256': sha256(control_audit),
            'fault_audited_core_sha256': sha256(fault_audit), 'post_abort_register_continuations': 296, 'post_abort_bus_continuations': 4,
            'audit': 'Read-only access/vector hooks; fault build also captures state after successful vector frame. Core step continues unchanged. Source emulator unchanged.'},
        'limits': ['Software traps/RTI are limited to one kernel register set, CM=PM=RS=T=0.',
            'No trace, HALT/RTT, PSW ISA beyond SPL, EIS or J-11 banking.',
            'Second bus/address error inside a vector frame uses diagnostic STOP; DCJ11 red/yellow stack recovery is not implemented.',
            'Fault comparison ends at the successful vector frame; later C execution after fAbort is logged separately. Internal DCJ11 CSRs excluded.',
            'Injected ACK errors model an external bus response. SPI FRAM has no physical ACK/CRC failure indication.',
            'No board programming, full peripheral synthesis top or external pin timing closure.'],
        'fixtures_uncompressed_sha256': fixtures,
        'vendor_models_sha256': {p.name: sha256(p.read_bytes()) for p in (ROOT/'build/vendor').glob('*.v')},
        'files': {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in sorted(files)}})
    print(f'CP16 recorded: {len(archives)} synthesis archives; {report_count} raw report hashes; '
          '124969 completed instructions plus 32780 fault-frame cases per memory/ROM pair; 726 benchmark runs per ROM.')


if __name__ == '__main__': main()
