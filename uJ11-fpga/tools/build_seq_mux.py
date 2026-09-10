#!/usr/bin/env python3
"""CP41: derive equivalent sequencer address muxes from frozen CP40 RTL."""
import hashlib
import json
import tarfile
from board_common import ROOT


def main():
    out=ROOT/'build/cp41-seq';out.mkdir(parents=True,exist_ok=True)
    archive_path=ROOT/'synth/reports/cp40i/source.tgz'
    with tarfile.open(archive_path) as archive:
        base=archive.extractfile('rtl/uj11_microseq.v').read().decode()
    expected=json.loads((ROOT/'synth/reports/cp40i/inputs.json').read_text())['files']['rtl/uj11_microseq.v']
    assert hashlib.sha256(base.encode()).hexdigest()==expected
    start=base.index('    always @* begin')
    finish=base.index('    always @(posedge clk)',start)
    selectors='''    // Mutually exclusive sources preserve every control encoding, including
    // reserved command values. The link register and its updates are unchanged.
    wire control=uword[35];
    wire [1:0] flow=uword[9:8];
    wire take_sequential=control ? (command==4'd1 && !condition) :
                         (flow==2'd0 || (flow==2'd3 && !a_one));
    wire take_page=!control && flow==2'd1;
    wire take_fetch=control ? (command==4'd14 && (trace_pending || irq_pending)) :
                    (flow==2'd2 || (flow==2'd3 && a_one));
    wire take_hold=control && (command==4'd13 ||
                   (command==4'd14 && !trace_pending && !irq_pending));
    wire take_link=control && command==4'd3 && link_valid;
    wire take_stop=control && ((command==4'd3 && !link_valid) ||
                   (command==4'd10 && link_valid));
    wire take_decode=control && (command==4'd2 || command==4'd4);
    wire take_target=control && !(take_sequential || take_fetch || take_hold ||
                      take_link || take_stop || take_decode);
'''
    mux='''        next_address = (sequential & {10{take_sequential}}) |
            ({upc[9:8],uword[7:0]} & {10{take_page}}) |
            (fetch_address & {10{take_fetch}}) |
            (upc & {10{take_hold}}) |
            (link & {10{take_link}}) |
            (STOP & {10{take_stop}}) |
            (dispatch_address & {10{take_decode}});
'''
    flat=selectors+'''    always @* begin
'''+mux+'''        next_address = next_address |
            ({target[9:3],target[2:0] | dispatch_bits} & {10{take_target}});
        if (fault_redirect) next_address = uword[2] ? target : 10'h015;
        if (fault_repair) next_address = 10'h015;
        if (reset)
            next_address = 10'b0;
    end
'''
    merged=selectors+'''    wire normal_flow=!(fault_redirect || fault_repair);
    wire repair_target=fault_redirect && uword[2] && !fault_repair;
    wire repair_vector=fault_repair || (fault_redirect && !uword[2]);
    always @* begin
'''+mux+'''        next_address = (next_address & {10{normal_flow}}) |
            (target & {10{(take_target && normal_flow) || repair_target}}) |
            ({7'b0,dispatch_bits} & {10{take_target && normal_flow}}) |
            (10'h015 & {10{repair_vector}});
        if (reset)
            next_address = 10'b0;
    end
'''
    encoded='''    reg [2:0] address_select;
    always @* begin
        address_select=3'd2; // target / OR-dispatch
        if (uword[35]) begin
            case (command)
                4'd1: if (!condition) address_select=3'd0;
                4'd10: if (link_valid) address_select=3'd7;
                4'd3: address_select=link_valid ? 3'd5 : 3'd7;
                4'd4,4'd2: address_select=3'd4;
                4'd13: address_select=3'd1;
                4'd14: address_select=(trace_pending || irq_pending) ? 3'd6 : 3'd1;
                default: begin end
            endcase
        end else begin
            case (uword[9:8])
                2'd0: address_select=3'd0;
                2'd1: address_select=3'd3;
                2'd2: address_select=3'd6;
                2'd3: address_select=a_one ? 3'd6 : 3'd0;
            endcase
        end
        // Balanced data mux; decoding selects only three control bits.
        next_address = address_select[2] ?
            (address_select[1] ? (address_select[0] ? STOP : fetch_address) :
                                (address_select[0] ? link : dispatch_address)) :
            (address_select[1] ? (address_select[0] ? {upc[9:8],uword[7:0]} :
                                  {target[9:3],target[2:0] | dispatch_bits}) :
                                (address_select[0] ? upc : sequential));
        if (fault_redirect) next_address = uword[2] ? target : 10'h015;
        if (fault_repair) next_address = 10'h015;
        if (reset)
            next_address = 10'b0;
    end
'''
    folded=encoded.replace('    reg [2:0] address_select;', """    reg [2:0] address_select;
    wire fault_vector=fault_repair || fault_redirect;
    wire [9:0] stop_or_vector=fault_vector ? 10'h015 : STOP;
    wire [2:0] normal_dispatch=dispatch_bits & {3{!fault_redirect}};""")
    folded=folded.replace('        // Balanced data mux;', """        if (fault_redirect) address_select=uword[2] ? 3'd2 : 3'd7;
        if (fault_repair) address_select=3'd7;
        // Balanced data mux;""")
    folded=folded.replace('address_select[0] ? STOP : fetch_address','address_select[0] ? stop_or_vector : fetch_address')
    folded=folded.replace('target[2:0] | dispatch_bits','target[2:0] | normal_dispatch')
    folded=folded.replace("        if (fault_redirect) next_address = uword[2] ? target : 10'h015;\n",'')
    folded=folded.replace("        if (fault_repair) next_address = 10'h015;\n",'')
    sources={'baseline':base,'flat':base[:start]+flat+base[finish:],
             'merged':base[:start]+merged+base[finish:],
             'encoded':base[:start]+encoded+base[finish:],
             'folded':base[:start]+folded+base[finish:]}
    outputs={}
    for kind,source in sources.items():
        folder=out/kind;folder.mkdir(exist_ok=True)
        path=folder/'uj11_microseq.v';path.write_text(source)
        # Exact CP35 context override, also retained by CP37 and CP39.
        context=source.replace('input wire clk, reset, enable,',
            'input wire clk, reset, enable, context_redirect,\n    input wire [9:0] context_address,')
        context=context.replace('        if (reset)\n            next_address',
            '        if (context_redirect) next_address = context_address;\n        if (reset)\n            next_address')
        (folder/'uj11_microseq_apr.v').write_text(context)
        for p in folder.glob('*.v'):outputs[str(p.relative_to(ROOT))]=hashlib.sha256(p.read_bytes()).hexdigest()
    inputs=['tools/build_seq_mux.py','synth/reports/cp40i/source.tgz','synth/reports/cp40i/inputs.json']
    (out/'inputs.json').write_text(json.dumps(dict(baseline_sha256=expected,
        inputs_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs},
        outputs_sha256=outputs),indent=2)+'\n')
    print('PASS CP41 candidates: baseline, flat, merged, encoded, folded; normal and context override variants')


if __name__=='__main__':main()
