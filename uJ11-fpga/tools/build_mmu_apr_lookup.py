#!/usr/bin/env python3
"""Build a read-only APR lookup cost floor, keeping production CP36 intact.

The scheduled variant reads EBR on APR_READ's edge and saves its output on
the next ALU edge, with no extra ready register or wait cycle.
No CPU CSR/write arbitration, translation, access checks or MMRs in this gate.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import build_mmu_entry
from build_mmu_entry import change, ROOT
from make_ebr import generate

sys.path.insert(0, str(ROOT/'microasm'))
from uj11asm import assemble as native_assemble
from uj11aprasm import assemble


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--page', action='store_true', help='retain VA page3 instead of RF A4 + read mux')
    parser.add_argument('--scheduled', action='store_true', help='use the ALU save word as EBR latency slot')
    parser.add_argument('--masked-d', action='store_true', help='CP42 split D-input and merge APR data after the selector')
    args = parser.parse_args()
    build_mmu_entry.main()
    out = ROOT/'build/cp37-lookup'
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT/'microcode/generated/full.uasm').read_text()
    helper = (ROOT/'microcode/mmu_apr_lookup.uasm').read_text()
    old, old_listing, old_labels, _ = native_assemble(source)
    image, listing, labels, stats = assemble(source+'\n'+helper)
    used = {int(line.split()[0], 16) for line in old_listing.splitlines()}
    assert all(image[a] == old[a] for a in used)
    assert all(labels[k] == v for k, v in old_labels.items())
    assert stats['used_words'] == 963
    # The retained address must never name private scratch. Reject later ISA
    # changes that invalidate that lifetime assumption before emitting RTL.
    memory = {a for a in used if old[a] >> 35 and (old[a] >> 31 & 15) in (2, 11, 12)}
    selectors = {old[a] >> 26 & 31 for a in memory}
    assert selectors == {6, 7, 9, 11, 12, 16, 17}
    assert all((old[a] >> 26 & 31) < 24 for a in used)
    assert not any(old[a] >> 35 and (old[a] >> 31 & 15) == 0 and old[a] & 32 for a in used)
    for filename, data in [('lookup.uasm', source+'\n'+helper), ('lookup.lst', listing),
                           ('lookup.mem', ''.join(f'{w:09x}\n' for w in image)),
                           ('lookup.labels.json', json.dumps(labels, indent=2)+'\n'),
                           ('lookup.stats.json', json.dumps(stats, indent=2)+'\n'),
                           ('uj11_m0_ebr.v', generate(image))]:
        (out/filename).write_text(data)
    for filename in ('uj11_core.v', 'uj11_microseq.v', 'uj11_decode_table.v'):
        (out/filename).write_bytes((ROOT/'build/cp35'/filename).read_bytes())
    engine = (ROOT/'build/cp35/uj11_engine.v').read_text()
    engine = change(engine, '    wire [9:0] mmu_context_address;', '''    wire [9:0] mmu_context_address;
    reg [3:0] mmu_memory_a;
    reg apr_ready;
    wire apr_request = running && mmu_active && control && command==0 && uword[5];
    wire apr_stall = apr_request && !apr_ready;
    wire [15:0] apr_data;
    // Kernel unified lookup profile. Other mode/I-D entries remain stored,
    // but selection and CPU CSR programming are not integrated in CP37.
    uj11_mmu_apr_ram apr_ram(.clk(clk),.enable(apr_request),
        .address({3'b000,read_a[15:13],uword[6]}),
        .write_enable(2'b00),.write_data(16'b0),.read_data(apr_data));
    always @(posedge clk) begin
        if (reset) apr_ready <= 0;
        else apr_ready <= apr_request;
        if (mmu_redirect && !mmu_active) mmu_memory_a <= a;
    end''')
    engine = change(engine, '.hold_routine(mmu_hold)', '.hold_routine(mmu_hold || apr_stall)')
    engine = change(engine, 'wire [3:0] a = select_register(a_select,{ir[8:6],ir[2:0]});',
                    '''wire [3:0] a = a_select[4] && a_select[3] ? mmu_memory_a :
                     select_register(a_select,{ir[8:6],ir[2:0]});''')
    engine = change(engine, "3'd0: d = 0;", "3'd0: d = mmu_active ? apr_data : 16'b0;")
    if args.page:
        engine = change(engine, 'reg [3:0] mmu_memory_a;', 'reg [2:0] mmu_page;')
        engine = change(engine, "{3'b000,read_a[15:13],uword[6]}", "{3'b000,mmu_page,uword[6]}")
        engine = change(engine, 'mmu_memory_a <= a;', 'mmu_page <= read_a[15:13];')
        engine = change(engine, '''wire [3:0] a = a_select[4] && a_select[3] ? mmu_memory_a :
                     select_register(a_select,{ir[8:6],ir[2:0]});''',
                        'wire [3:0] a = select_register(a_select,{ir[8:6],ir[2:0]});')
    if args.scheduled:
        engine = change(engine, '    reg apr_ready;\n', '')
        engine = change(engine, '    wire apr_stall = apr_request && !apr_ready;\n', '')
        engine = change(engine, '        if (reset) apr_ready <= 0;\n        else apr_ready <= apr_request;\n', '')
        engine = change(engine, '.hold_routine(mmu_hold || apr_stall)', '.hold_routine(mmu_hold)')
    if args.masked_d:
        engine = change(engine, "3'd0: d = mmu_active ? apr_data : 16'b0;", "3'd0: d = 0;")
        # CP42: high byte has only MDR, PSW and the displacement sign.
        # Keep this change local to the experimental APR engine: the full
        # production gate did not save LUT. The native engine remains intact.
        start = engine.index('    always @* begin\n        case (uword[12:10])')
        end = engine.index('    uj11_datapath dp(', start)
        engine = engine[:start]+'''    wire [2:0] d_select=uword[12:10];
    wire d_byte_step=byte_instruction && a<4'd6;
    wire [1:0] d_small={d_select[1] && (!d_select[0] || !d_byte_step),
                        d_select[0] && (!d_select[1] || d_byte_step)};
    wire [7:0] d_disp_low={ir[6] && !ir[14],ir[5:0],1'b0};
    always @* begin
        d[7:0]=d_select[2] ?
            (d_select[1] ? (d_select[0] ? psw[7:0] : uword[7:0]) :
                           (d_select[0] ? d_disp_low : mdr[7:0])) : {6'b0,d_small};
        // Only MDR, PSW and the replicated displacement sign reach the high byte.
        d[15:8]=(mdr[15:8] & {8{d_select==3'd4}}) |
                (psw[15:8] & {8{d_select==3'd7}}) |
                {8{d_select==3'd5 && ir[7] && !ir[14]}};
    end
'''+engine[end:]
        engine = change(engine, '    uj11_datapath dp(', '''    wire [15:0] lookup_d = d | (apr_data & {16{mmu_active && uword[12:10]==0}});
    uj11_datapath dp(''')
        engine = change(engine, '.destination(destination),.d(d),', '.destination(destination),.d(lookup_d),')
    engine = change(engine, 'build/cp35/entry.mem', 'build/cp37-lookup/lookup.mem')
    (out/'uj11_engine.v').write_text(engine)
    inputs = ['tools/build_mmu_apr_lookup.py', 'microasm/uj11aprasm.py',
              'microcode/mmu_apr_lookup.uasm', 'rtl/uj11_mmu_apr_ram.v',
              'build/cp35/inputs.json', 'build/cp35/uj11_engine.v',
              'build/cp35/uj11_core.v', 'build/cp35/uj11_microseq.v', 'build/cp35/uj11_decode_table.v']
    manifest = dict(scope='Read-only APR lookup cost floor; no CPU CSR/translation/MMR',
                    retained_address='page3' if args.page else 'selector4',
                    scheduled_read=args.scheduled,
                    masked_d=args.masked_d,
                    original_words_unchanged=954, microcode_words=963,
                    helper_executed_words=9, overhead_without_external_hold=10 if args.scheduled else 12,
                    memory_words=len(memory), memory_a_selectors=sorted(selectors),
                    inputs_sha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
                    outputs_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(out.glob('*')) if p.is_file() and p.name != 'inputs.json'})
    (out/'inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('PASS CP37 build: 954 guest words intact; 9 helper words; kernel unified APR read path')


if __name__ == '__main__':
    main()
