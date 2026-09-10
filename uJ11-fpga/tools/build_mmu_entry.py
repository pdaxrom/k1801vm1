#!/usr/bin/env python3
"""Derive a CP35 test/synthesis candidate without modifying the production core.

Every edit is an exact, counted substitution into the current core/seq/engine.
The ordinary microcode must remain byte-identical; the helper occupies holes.
"""
import hashlib
import json
import sys
from pathlib import Path
from make_ebr import generate

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble as native_assemble
from uj11entryasm import assemble


def change(source, before, after):
    assert source.count(before) == 1, before
    return source.replace(before, after)


def main():
    out = ROOT/'build/cp35'
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT/'microcode/generated/full.uasm').read_text()
    helper = (ROOT/'microcode/mmu_entry.uasm').read_text()
    old, old_listing, old_labels, old_stats = native_assemble(source)
    assert old == [int(w, 16) for w in (ROOT/'microcode/generated/m0.mem').read_text().split()]
    image, listing, labels, stats = assemble(source+'\n'+helper)
    used = {int(line.split()[0], 16) for line in old_listing.splitlines()}
    assert all(image[a] == old[a] for a in used)
    assert all(labels[k] == v for k, v in old_labels.items())
    assert stats['used_words'] == old_stats['used_words']+9 == 963
    assert labels['MMU_ENTRY'] == 0x1cd and labels['MMU_ENTRY_RESTORE'] == 0x32d
    assert sum(bool(w >> 35 and w & 2) for w in image) == 1
    (out/'entry.uasm').write_text(source+'\n'+helper)
    (out/'entry.mem').write_text(''.join(f'{w:09x}\n' for w in image))
    (out/'entry.lst').write_text(listing)
    (out/'entry.labels.json').write_text(json.dumps(labels, indent=2)+'\n')
    (out/'entry.stats.json').write_text(json.dumps(stats, indent=2)+'\n')
    (out/'uj11_m0_ebr.v').write_text(generate(image))

    core = (ROOT/'rtl/uj11_core.v').read_text()
    core = change(core, 'input wire clk, reset,', 'input wire clk, reset, mmu_enabled, mmu_hold,')
    core = change(core, '    wire [15:0] dispatch_ir;',
                  '    wire [15:0] dispatch_ir;\n    wire unused_dispatch=^dispatch_ir;')
    baseline = out/'baseline'
    baseline.mkdir(exist_ok=True)
    (baseline/'uj11_core.v').write_text(change(core, '    wire [15:0] dispatch_ir;',
        '    wire unused_context_controls=^{mmu_enabled,mmu_hold};\n    wire [15:0] dispatch_ir;'))
    core = change(core, 'engine(.clk(clk),.reset(reset),',
                  'engine(.clk(clk),.reset(reset),.mmu_enabled(mmu_enabled),.mmu_hold(mmu_hold),')
    (out/'uj11_core.v').write_text(core)
    # The common generated ROM keeps an IMAGE compatibility parameter that
    # is intentionally ignored. Scope this lint annotation to its declaration.
    table = (ROOT/'microcode/generated/uj11_decode_table.v').read_text()
    declaration = 'module uj11_decode_table #(parameter IMAGE = "unused") ('
    table = change(table, declaration,
                   '/* verilator lint_off UNUSEDPARAM */\n'+declaration+'\n/* verilator lint_on UNUSEDPARAM */')
    (out/'uj11_decode_table.v').write_text(table)

    seq = (ROOT/'rtl/uj11_microseq.v').read_text()
    seq = change(seq, 'input wire clk, reset, enable,',
                 'input wire clk, reset, enable, context_redirect,\n    input wire [9:0] context_address,')
    seq = change(seq, '        if (reset)\n            next_address',
                 '        if (context_redirect) next_address = context_address;\n        if (reset)\n            next_address')
    (out/'uj11_microseq.v').write_text(seq)

    engine = (ROOT/'rtl/uj11_engine.v').read_text()
    engine = change(engine, 'input wire clk, reset,', 'input wire clk, reset, mmu_enabled, mmu_hold,')
    marker = '    wire memory_op = fetching || reading || writing;'
    engine = change(engine, marker, marker+'''
    wire mmu_active, mmu_block_memory, mmu_stall, mmu_redirect;
    wire [9:0] mmu_context_address;
    uj11_mmu_entry entry(.clk(clk),.reset(reset),.enabled(mmu_enabled),
        .running(running),.memory_word(memory_op),.return_word(control && uword[1]),
        .hold_routine(mmu_hold),.guest_advance(advance),.upc(debug_upc),
        .redirect(mmu_redirect),.redirect_address(mmu_context_address),
        .active(mmu_active),.block_memory(mmu_block_memory),.stall(mmu_stall));''')
    engine = change(engine, 'wire step = running && ready && bus_fault==0;',
                    'wire step = running && ready && bus_fault==0 && !mmu_stall;')
    engine = change(engine, 'wire advance = running && ready && !(frame_active && bus_fault!=0);',
                    'wire advance = (running && ready && !(frame_active && bus_fault!=0) || mmu_redirect) && !mmu_stall;')
    engine = change(engine, 'wire byte_operation = !control && byte_instruction &&',
                    'wire byte_operation = !control && !mmu_active && byte_instruction &&')
    engine = change(engine, 'memory(.active(memory_op && !reset && !stopped),',
                    'memory(.active(memory_op && !reset && !stopped && !mmu_block_memory),')
    engine = change(engine, 'seq(.clk(clk),.reset(reset),.enable(advance),',
                    'seq(.clk(clk),.reset(reset),.enable(advance),\n        .context_redirect(mmu_redirect),.context_address(mmu_context_address),')
    engine = change(engine, '.IMAGE("microcode/generated/m0.mem")', '.IMAGE("build/cp35/entry.mem")')
    (out/'uj11_engine.v').write_text(engine)
    inputs = ['rtl/uj11_core.v', 'rtl/uj11_engine.v', 'rtl/uj11_microseq.v',
              'rtl/experimental/uj11_mmu_entry.v', 'microasm/uj11asm.py', 'microasm/uj11entryasm.py',
              'microcode/m0.uasm', 'microcode/fis.uasm', 'microcode/mmu_entry.uasm',
              'microcode/generated/full.uasm', 'microcode/generated/uj11_decode_table.v',
              'tools/link_fis.py', 'tools/make_ebr.py', 'tools/build_mmu_entry.py']
    manifest = dict(scope='CP35 context hook only; no address translation',
                    microcode_words=963, original_words_unchanged=954,
                    routine_dynamic_words=8, overhead_without_hold_clocks=9,
                    inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                    outputs_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'inputs.json'})
    (out/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('PASS CP35 build: production 954 words intact; 9 helper words, 8 executed; private return in 36 bits')


if __name__ == '__main__':
    main()
