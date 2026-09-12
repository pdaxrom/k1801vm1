#!/usr/bin/env python3
"""Freeze a successful CP60 run, its exact sources, programs and transcripts."""
import argparse
import hashlib
import json
import re
import shutil
import tarfile
from pathlib import Path
from board_common import ROOT


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    parser.add_argument('--asm-dir', required=True, type=Path)
    parser.add_argument('--compat', required=True, type=Path)
    parser.add_argument('--out', type=Path, default=ROOT/'tb/reports/cp60')
    args = parser.parse_args()
    run, asm, compat, out = [p.resolve() for p in (args.run, args.asm_dir, args.compat, args.out)]
    result = json.loads((run/'result.json').read_text())
    inputs = json.loads((run/'inputs.json').read_text())
    build = json.loads((asm/'build-inputs.json').read_text())
    compatibility = json.loads((compat/'result.json').read_text())
    assert result['passed'] and compatibility['passed'] and inputs['rtl_changed']
    gate = json.loads((ROOT/'synth/reports/cp60b/result.json').read_text())
    assert gate['timing_pass'] and gate['fully_routed'] and gate['applied_clock_mhz'] == 31.824
    assert gate['constraint_matched'] and gate['ebr'] == 6
    for name, digest in inputs['profile']['outputs'].items():
        assert sha(ROOT/name) == digest == gate['inputs']['files'][name], name
    for name, digest in result['files'].items():
        assert sha(run/name) == digest, name
    for name, digest in inputs['files'].items():
        assert sha(ROOT/name) == digest, name
    assert inputs['build'] == build
    for name, digest in build['outputs'].items():
        assert sha(asm/name) == digest, name
    for name, digest in build['source_sha256'].items():
        assert sha(ROOT/name) == digest, name
    assert sha(ROOT/'tools/rt11_build.py') == build['driver_sha256']
    assert sha(ROOT/'demos/rt11/service/UJLOAD.SAV') == build['outputs']['UJLOAD.SAV'] == compatibility['sav_sha256']
    assert sha(compat/'uart.log') == compatibility['uart_sha256']
    assert sha(ROOT/'tools/test_loader_compat_cp60.py') == compatibility['driver_sha256']
    out.mkdir(parents=True, exist_ok=False)
    for name in ('inputs.json', 'result.json', 'build.log', 'simulation.log', 'uart.txt', 'directory.txt', 'symbols.json'):
        shutil.copyfile(run/name, out/name)
    (out/'asm').mkdir()
    for name in list(build['outputs']) + ['build-inputs.json', 'console.log']:
        shutil.copyfile(asm/name, out/'asm'/name)
    (out/'compat').mkdir()
    for name in ('result.json', 'uart.log'):
        shutil.copyfile(compat/name, out/'compat'/name)
    names = set(inputs['files']) | set(build['source_sha256']) | {
        'tools/rt11_build.py', 'tools/test_loader_compat_cp60.py',
        'tools/archive_loader_cp60.py', 'tools/board_common.py', 'tools/build_rk_recovery_cp60.py',
        'tools/build_service_disk.py', 'tools/build_panel_image.py', 'tb/test_service_image.py',
        'demos/rt11/service/UJLOAD.SAV', 'build/cp60-loader/cases.json'}
    (out/'board').mkdir()
    for filename in ('rk-tests.json','service-tests.json','bench-results.json','bench-portable.json','bench-vendor.json',
                     'rk-portable.log','rk-vendor.log','rk-broken-clear.log','service-portable.log','service-vendor.log',
                     'bench.log','rk-portable-build.log','rk-vendor-build.log','rk-broken-clear-build.log',
                     'service-portable-build.log','service-vendor-build.log'):
        shutil.copyfile(ROOT/'build/cp60-rk'/filename, out/'board'/filename)
    shutil.copyfile(ROOT/'build/cp60b/netlist.json', out/'board/netlist.json')
    shutil.copyfile(ROOT/'build/cp60-tools/unit.log', out/'unit.log')
    for name in ('production','panel-regression','service-disk-test'):
        shutil.copyfile(ROOT/('build/cp60-'+name+'.json'), out/(name+'.json'))
    names.update(['tools/test_rk_recovery_cp60.py','tools/test_service_board_cp60.py','tools/benchmark_board_cp60.py'])
    for r in json.loads((out/'board/rk-tests.json').read_text()):
        for name,digest in r['sources'].items():
            assert sha(ROOT/name) == digest, name
            names.add(name)
    for r in json.loads((out/'board/service-tests.json').read_text()):
        for name,digest in r['files'].items():
            assert sha(ROOT/name) == digest, name
            names.add(name)
    bench = json.loads((out/'board/bench-results.json').read_text())
    assert bench['profile_sha256'] == sha(ROOT/'build/cp60-rk/inputs.json')
    for r in bench['runs']:
        for name,digest in r['sources_sha256'].items():
            assert sha(ROOT/name) == digest, name
            names.add(name)
    with tarfile.open(out/'source.tgz', 'w:gz') as archive:
        for name in sorted(names):
            path = ROOT/name
            assert path.resolve().is_relative_to(ROOT), name
            archive.add(path, arcname=name, recursive=False)
    log = (run/'simulation.log').read_text()
    values = re.search(r'PASS CP60 full RT-11 loader: (\d+) file/command cases.*?; (\d+) clocks, (\d+) retired, (\d+) upper writes, (\d+) wire bytes', log)
    assert values, 'missing complete summary'
    record = dict(checkpoint='CP60', rtl_changed=True, new_synthesis=True,
                  synthesis_reference='CP60b', baseline='CP59d', cases=int(values[1]), clocks=int(values[2]),
                  retired=int(values[3]), upper_writes=int(values[4]), uart_bytes=int(values[5]),
                  source_files={name: sha(ROOT/name) for name in sorted(names)},
                  archived_files={str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (out/'archive.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps({k:v for k,v in record.items() if k not in ('source_files','archived_files')}, indent=2))


if __name__ == '__main__':
    main()
