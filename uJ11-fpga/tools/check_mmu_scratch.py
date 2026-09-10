#!/usr/bin/env python3
"""Poison dead temporaries at real CPU memory boundaries; reuse ISA/bus oracles.

Only testbench copies in build/ are instrumented. Production RTL/ROM and the
original fixture expectations are unchanged. +poison_live also corrupts T0
and must fail the FIS oracle, using the very same compiled executable.
"""
import hashlib
import json
import re
import subprocess
from board_common import ROOT, CORE
from check_sync_decode import MEMORY
from run_fis_tests import compile_test, execute


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def instrument(name):
    original = (ROOT/f'tb/{name}.v').read_text()
    assert original.count('endmodule') == 1
    active = "1'b1" if name == 'tb_core' else '(active != 0)'
    hook = f'''
    // Test-only fault injection after fixture setup, before the next CPU edge.
    integer poison_events=0, poison_live=0, poison_index;
    reg [1023:0] poison_seen=0;
    initial poison_live=$test$plusargs("poison_live");
    always @(negedge clk) begin
        #1;
        if(!reset && {active} && dut.engine.memory_op) begin
            poison_events=poison_events+1;
            poison_seen[upc]=1'b1;
            dut.engine.dp.rf.words[13]=16'(poison_events)^16'ha55a;
            dut.engine.dp.rf.words[14]=~16'(poison_events)^16'h1234;
            dut.engine.dp.rf.words[15]=16'(poison_events*17)^16'hfedc;
            if(poison_live!=0) dut.engine.dp.rf.words[8]=16'hdead;
        end
    end
    final begin
        $display("SCRATCH poison_events=%0d live_mutation=%0d",poison_events,poison_live);
        for(poison_index=0;poison_index<1024;poison_index=poison_index+1)
            if(poison_seen[poison_index])$display("SCRATCH upc=%03x",poison_index);
    end
'''
    path = ROOT/f'build/cp34-scratch-{name}.v'
    path.write_text(original.replace('endmodule', hook+'endmodule'))
    return path


def main():
    (ROOT/'build').mkdir(exist_ok=True)
    subprocess.run(['python3', 'tools/audit_mmu_scratch.py'], cwd=ROOT, check=True)
    cases = [('core', {}, 'isa_vectors.mem'),
             ('ea', {'SUITE': 0}, 'ea-vectors.txt'),
             ('ea', {'SUITE': 9}, 'irq-vectors.txt'),
             ('trace_bit', {}, 'trace_bit-vectors.txt'),
             ('bus_fault', {}, 'bus-fault-vectors.txt')]
    for eis in ('ASH', 'ASHC', 'XOR', 'MUL', 'DIV'):
        cases += [('trace_bit', {f'EIS_{eis}': 1}, f'eis_{eis.lower()}-vectors.txt'),
                  ('bus_fault', {f'EIS_{eis}': 1}, f'eis-{eis.lower()}-fault-vectors.txt')]
    paths = set(CORE+MEMORY+['rtl/uj11_rom.v', 'microcode/generated/m0.mem',
                           'microcode/generated/decode.mem', 'tools/check_mmu_scratch.py',
                           'tools/audit_mmu_scratch.py', 'tools/board_common.py',
                           'tools/check_sync_decode.py', 'tools/run_fis_tests.py',
                           'tb/tb_fis.v', 'build/fis-vectors.txt'])
    paths.update(f'tb/tb_{top}.v' for top, _, _ in cases)
    paths.update(f'build/{fixture}' for _, _, fixture in cases)
    before = {p: digest(ROOT/p) for p in sorted(paths)}
    outcomes, seen = [], set()

    def collect(tag):
        log = (ROOT/f'build/{tag}.log').read_text()
        assert 'PASS' in log and 'FATAL' not in log and 'Error' not in log, tag
        match = re.search(r'SCRATCH poison_events=(\d+) live_mutation=0', log)
        assert match and int(match[1]) > 0, (tag, 'injection inactive')
        # Icarus prints an integer as eight hex digits even with %03x;
        # Verilator prints three. Parse the entire number, then normalize.
        covered = sorted({f'{int(value, 16):03x}' for value in
                          re.findall(r'^SCRATCH upc=([0-9a-f]+)$', log, re.MULTILINE)})
        seen.update(covered)
        outcome = dict(tag=tag, poison_events=int(match[1]), memory_upcs=covered,
                       pass_lines=[line for line in log.splitlines() if line.startswith('PASS')])
        outcomes.append(outcome)
        print('; '.join(outcome['pass_lines'])+f'; poison events={match[1]}', flush=True)

    for top, parameters, _ in cases:
        name = 'tb_'+top
        tag = 'cp34-scratch-'+top+''.join('-'+k+'-'+str(v) for k,v in parameters.items())
        flags = [f'-P{name}.ROM_DECODE=1']+[f'-P{name}.{k}={v}' for k,v in parameters.items()]
        with (ROOT/f'build/{tag}-build.log').open('w') as log:
            subprocess.run(['iverilog', '-g2012', '-Wall', '-s', name, '-o', 'build/'+tag]+
                           flags+[str(instrument(name))]+CORE+MEMORY+['rtl/uj11_rom.v'],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (ROOT/f'build/{tag}.log').open('w') as log:
            subprocess.run(['vvp', 'build/'+tag], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        collect(tag)

    # Use the production image (CANDIDATE_ROM=0), with synchronous decoder.
    joined = ROOT/'build/cp34-scratch-core-sync.v'
    joined.write_text('\n'.join((ROOT/s).read_text() for s in
                     ['rtl/uj11_core.v', 'rtl/uj11_decode_rom.v', 'microcode/generated/uj11_decode_table.v']))
    tb = instrument('tb_fis')
    tb.write_text(tb.read_text().replace('ROM_DECODE=0', 'ROM_DECODE=1', 1))
    tag = 'cp34-scratch-fis'
    command = compile_test(-1, 'verilator', False, candidate=False, tag=tag,
                           replacements={'rtl/uj11_core.v': str(joined), 'tb/tb_fis.v': str(tb)})
    execute(command, tag, 'build/fis-vectors.txt')
    collect(tag)
    negative = 'cp34-scratch-live-negative'
    with (ROOT/f'build/{negative}.log').open('w') as log:
        result = subprocess.run(command+['+poison_live', '+vectors=build/fis-vectors.txt',
                                         f'+results=build/{negative}.csv'],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    log = (ROOT/f'build/{negative}.log').read_text()
    assert result.returncode != 0 and 'FIS case' in log and '%Error' in log, 'missed live T0 corruption'
    after = {p: digest(ROOT/p) for p in sorted(paths)}
    assert before == after, 'production source/fixtures modified during test'
    audit = json.loads((ROOT/'build/cp34-scratch.json').read_text())
    declared = {entry['upc'] for entry in audit['boundaries']}
    assert seen <= declared
    report = dict(scope='Test-only T5/T6/T7 poisoning during current CPU memory microinstructions',
                  rom_decode=1, microcode='production m0.mem', production_inputs_unchanged=True,
                  tests=outcomes, memory_upcs_covered=sorted(seen),
                  memory_upcs_not_exercised=sorted(declared-seen),
                  live_t0_negative_detected=True, negative_returncode=result.returncode,
                  inputs_sha256=before,
                  limits=['Not MMU entry/return, translation or RT-11XM execution.',
                          'Finite architectural fixtures supplement the conservative static graph.',
                          'Only T5/T6/T7 are overwritten; no permission to clobber other state.'])
    (ROOT/'build/cp34-scratch-dynamic.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS scratch preservation: {len(outcomes)} suites; {len(seen)}/{len(declared)} memory words exercised; live T0 mutation rejected', flush=True)


if __name__ == '__main__':
    main()
