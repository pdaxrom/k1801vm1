#!/usr/bin/env python3
"""Exercise every remaining memory uPC with the CP34 temporary-poison hook.

Run after check_mmu_scratch.py: common build-only testbench copies are reused.
Architectural coverage is finite; the ROM liveness audit remains separate.
"""
import json
import re
import subprocess
from check_mmu_scratch import ROOT, CORE, MEMORY, digest, instrument


def main():
    cases = [('ea', {'SUITE': 1}, 'single-vectors.txt'),
             ('ea', {'SUITE': 6}, 'control-vectors.txt'),
             ('ea', {'SUITE': 7}, 'extra-vectors.txt'),
             ('trace_bit', {'PSW_TRANSFER': 1}, 'psw_transfer-vectors.txt'),
             ('trace_bit', {'SYSTEM_CONTROL': 1}, 'system_control-vectors.txt')]
    files = set(CORE+MEMORY+['rtl/uj11_rom.v', 'microcode/generated/m0.mem',
                            'microcode/generated/decode.mem', 'tools/check_mmu_scratch.py',
                            'tools/check_mmu_scratch_coverage.py', 'tools/board_common.py',
                            'tools/check_sync_decode.py', 'build/cp34-scratch.json',
                            'build/cp34-scratch-dynamic.json'])
    files.update(f'tb/tb_{top}.v' for top, _, _ in cases)
    files.update(f'build/{fixture}' for _, _, fixture in cases)
    before = {p: digest(ROOT/p) for p in sorted(files)}
    prior = json.loads((ROOT/'build/cp34-scratch-dynamic.json').read_text())
    assert prior['live_t0_negative_detected']
    for path, expected in prior['inputs_sha256'].items():
        assert digest(ROOT/path) == expected, path
    seen = set(prior['memory_upcs_covered'])
    tests = []
    for top, parameters, _ in cases:
        name = 'tb_'+top
        tag = 'cp34-scratch-coverage-'+top+''.join('-'+k+'-'+str(v) for k,v in parameters.items())
        flags = [f'-P{name}.ROM_DECODE=1']+[f'-P{name}.{k}={v}' for k,v in parameters.items()]
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog', '-g2012', '-Wall', '-s', name, '-o', 'build/'+tag]+
                           flags+[str(instrument(name))]+CORE+MEMORY+['rtl/uj11_rom.v'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(['vvp', 'build/'+tag], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        log = (ROOT/f'build/{tag}.log').read_text()
        assert 'PASS' in log and 'FATAL' not in log, tag
        match = re.search(r'SCRATCH poison_events=(\d+) live_mutation=0', log)
        assert match and int(match[1]) > 0, tag
        covered = sorted({f'{int(value, 16):03x}' for value in
                          re.findall(r'^SCRATCH upc=([0-9a-f]+)$', log, re.MULTILINE)})
        seen.update(covered)
        outcome = dict(tag=tag, poison_events=int(match[1]), memory_upcs=covered,
                       pass_lines=[line for line in log.splitlines() if line.startswith('PASS')])
        tests.append(outcome)
        print('; '.join(outcome['pass_lines'])+f'; poison events={match[1]}', flush=True)
    assert before == {p: digest(ROOT/p) for p in before}
    audit = json.loads((ROOT/'build/cp34-scratch.json').read_text())
    declared = {entry['upc'] for entry in audit['boundaries']}
    assert seen == declared, ('missing memory words', sorted(declared-seen))
    report = dict(scope='Additional fixtures for full memory-uPC poisoning coverage',
                  rom_decode=1, production_inputs_unchanged=True, tests=tests,
                  aggregate_memory_upcs_covered=sorted(seen), inputs_sha256=before,
                  limits=['All memory words exercised, not exhaustive architectural-state coverage.',
                          'CPU remains without MMU; poisoning validates only T5/T6/T7 lifetime.'])
    (ROOT/'build/cp34-scratch-coverage.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS scratch memory-word coverage: {len(seen)}/{len(declared)} including primary suites', flush=True)


if __name__ == '__main__':
    main()
