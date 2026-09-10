#!/usr/bin/env python3
"""Run existing CP44 CPU/bus/cold-FB tests against one CP45 bus candidate.

Generated drivers use distinct output paths; CP44 inputs/results are preserved.
The verification manifest includes both the original and generated driver.
"""
import argparse
import hashlib
import json
import sys
import tarfile
import types
from board_common import ROOT, CORE, BOARD
from build_bus_cp45 import adapt, VARIANTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=VARIANTS[1:], required=True)
    parser.add_argument('--suite', choices=('cpu', 'bus', 'board'), required=True)
    parser.add_argument('--vendor', action='store_true')
    parser.add_argument('--edges', action='store_true')
    parser.add_argument('--words', type=int, default=4096)
    args = parser.parse_args()
    prefix = 'cp45-'+args.variant
    original = dict(cpu='tools/check_relocate_cp44.py', bus='tools/check_relocate_bus_cp44.py',
                    board='tools/run_board.py')[args.suite]
    code = (ROOT/original).read_text()
    extra = ['tools/check_bus_cp45_system.py', 'tools/build_bus_cp45.py',
             'build/cp45-bus/inputs.json', original]
    if args.suite == 'cpu':
        code = code.replace('from build_direct_relocate_cp44 import adapt as direct_adapt',
                            'from build_bus_cp45 import adapt as direct_adapt')
        code = code.replace('direct_adapt(CORE,BOARD)', f'direct_adapt(CORE,BOARD,{args.variant!r})')
        code = code.replace("tag='cp44-'", f'tag={prefix+"-"!r}')
        code = code.replace('cp44-test-reference', prefix+'-test-reference')
        argv = ['--direct', '--words', str(args.words)]
        if args.vendor: argv.append('--vendor')
        if args.edges: argv.append('--edges')
        tag = prefix+'-direct-cpu-'+('vendor' if args.vendor else 'portable')
        tag += '-edges' if args.edges else '-'+str(args.words)
        manifest = tag+'.json'; key = 'inputs_sha256'
    elif args.suite == 'bus':
        code = code.replace('from build_direct_relocate_cp44 import adapt', 'from build_bus_cp45 import adapt')
        code = code.replace('adapt(CORE,BOARD)', f'adapt(CORE,BOARD,{args.variant!r})')
        code = code.replace('cp44-bus', prefix+'-bus')
        argv = []; manifest = prefix+'-bus.json'; key = 'inputs_sha256'
    else:
        with tarfile.open(ROOT/'tb/reports/cp44/test-sources.tgz') as archive:
            data = archive.extractfile('build/tb_board_rt11_cp44.v').read()
        frozen = json.loads((ROOT/'docs/verification-cp44.json').read_text())['sources_sha256']
        assert hashlib.sha256(data).hexdigest() == frozen['build/tb_board_rt11_cp44.v']
        tb = 'build/'+prefix+'-board-tb.v'; (ROOT/tb).write_bytes(data)
        code = code.replace('tb/tb_board_rt11.v', tb)
        extra.extend([tb, 'tb/reports/cp44/test-sources.tgz', 'docs/verification-cp44.json'])
        argv = ['--tag', prefix]; manifest = prefix+'-board-inputs.json'; key = 'files'
    generated = 'build/'+prefix+'-'+args.suite+'-driver.py'
    (ROOT/generated).write_text(code); extra.append(generated)
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in extra}
    driver = types.ModuleType('cp45_driver')
    exec(compile(code, generated, 'exec'), driver.__dict__)
    if args.suite == 'board': driver.CORE, driver.BOARD = adapt(CORE, BOARD, args.variant)
    sys.argv = [generated]+argv
    driver.main()
    assert hashes == {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in hashes}
    result = json.loads((ROOT/'build'/manifest).read_text()); result[key].update(hashes)
    result.update(cp45_variant=args.variant, cp45_suite=args.suite)
    final = manifest.replace('-inputs.json', '-verified.json') if args.suite == 'board' else manifest
    (ROOT/'build'/final).write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__': main()
