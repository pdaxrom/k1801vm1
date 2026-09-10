#!/usr/bin/env python3
"""CP36: two logic forms of a compact opcode-ROM index, exhaustively checked."""
import hashlib
import json
import subprocess
from board_common import ROOT
from make_ebr import generate


def opcode_index(op):
    group = op >> 12
    memory = int(bool(op & 0o70))
    if group in (0, 8):
        return (0x100 | ((op >> 15) << 7) | (((op >> 6) & 63) << 1) | memory
                if op >> 8 else 0x300 | (op & 255))
    if group == 7:
        return 0x200 | (((op >> 9) & 7) << 3) | (int(bool(op & 0o700)) << 2) | (((op >> 5) & 1) << 1) | memory
    # Group 15 is reserved just like the old single row at 0x240. Four
    # otherwise unused double-operand rows avoid a separate address mux.
    return (group << 4) | (int(bool(op & 0o7000)) << 3) | (memory << 2)


HEADER = '''`timescale 1ns/1ps
// CP36: every opcode and enable hold is checked against the old dispatch.
module uj11_decode_rom(input wire clk, enable,
    input wire [15:0] incoming, output wire [9:0] entry);
    wire [15:0] op = incoming;
    wire system_low = ~|op[15:8];
    wire single_group = ~|op[14:12];
    wire eis_group = op[15:12]==4'h7;
    wire memory_mode = |op[5:3];
'''
PLANES = '''    wire single_page = single_group && !system_low;
    wire double_page = !single_group && !eis_group;
    wire [9:0] index =
        ({2'b11,op[7:0]} & {10{system_low}}) |
        ({2'b01,op[15],op[11:6],memory_mode} & {10{single_page}}) |
        ({4'b1000,op[11:9],(|op[8:6]),op[5],memory_mode} & {10{eis_group}}) |
        ({2'b00,op[15:12],(|op[11:9]),memory_mode,2'b0} & {10{double_page}});
'''
BITS = '''    wire [9:0] index;
    assign index[9] = system_low || eis_group;
    assign index[8] = single_group;
    // EIS already has op[15]=0; both other non-system pages use op[15].
    assign index[7] = system_low ? op[7] : op[15];
    assign index[6] = system_low ? op[6] : single_group ? op[11] : (!eis_group && op[14]);
    assign index[5] = system_low ? op[5] : single_group ? op[10] : eis_group ? op[11] : op[13];
    assign index[4] = system_low ? op[4] : single_group ? op[9] : eis_group ? op[10] : op[12];
    assign index[3] = system_low ? op[3] : single_group ? op[8] : eis_group ? op[9] : (|op[11:9]);
    assign index[2] = system_low ? op[2] : single_group ? op[7] : eis_group ? (|op[8:6]) : memory_mode;
    assign index[1] = system_low ? op[1] : single_group ? op[6] : (eis_group && op[5]);
    assign index[0] = system_low ? op[0] : ((single_group || eis_group) && memory_mode);
'''
FOOTER = '''    uj11_decode_table table_rom(.clk(clk),.enable(enable),.address(index),.data(entry[8:0]));
    assign entry[9]=0;
endmodule
'''


def main():
    out = ROOT/'build/cp36-decode'
    out.mkdir(parents=True, exist_ok=True)
    paths = ['rtl/uj11_decode.v', 'tools/build_decode_compact.py', 'tools/make_ebr.py']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    (out/'dump.v').write_text('''module dump;
reg [15:0] ir; wire [9:0] entry; integer i;
uj11_decode dut(ir,entry);
initial begin for(i=0;i<65536;i=i+1)begin ir=i[15:0];#1;$display("%03x",entry);end $finish;end
endmodule
''')
    subprocess.run(['iverilog','-g2012','-s','dump','-o',str(out/'dump'),str(out/'dump.v'),
                    str(ROOT/'rtl/uj11_decode.v')],check=True)
    lines = subprocess.check_output(['vvp',str(out/'dump')],text=True).splitlines()
    truth = [int(s,16) for s in lines if len(s)==3]
    assert len(truth)==65536 and max(truth)<512
    seen = {}
    words = [0x42]*1024
    for op, entry in enumerate(truth):
        index = opcode_index(op)
        assert index not in seen or seen[index]==entry, (oct(op),index,entry,seen.get(index))
        seen[index]=words[index]=entry
    (out/'decode.mem').write_text(''.join(f'{w:03x}\n' for w in words))
    packed = generate(words).split('    DP8KC #(',2)
    header = packed[0].replace('uj11_rom','uj11_decode_table').replace('[35:0]','[8:0]')
    table = header+'''`ifdef SYNTHESIS
`define UJ11_DECODE_EBR
`elsif UJ11_VENDOR_ROM
`define UJ11_DECODE_EBR
`endif
`ifdef UJ11_DECODE_EBR
'''+ '    DP8KC #('+packed[1]+'''`else
    reg [8:0] words[0:1023]; reg [8:0] value;
    initial $readmemh("build/cp36-decode/decode.mem",words);
    always @(posedge clk) if(enable) value<=words[address];
    assign data=value;
`endif
`undef UJ11_DECODE_EBR
endmodule
'''
    (out/'uj11_decode_table.v').write_text(table)
    for variant, body in [('planes',PLANES), ('bits',BITS)]:
        folder = out/variant
        folder.mkdir(exist_ok=True)
        (folder/'uj11_decode_rom.v').write_text(HEADER+body+FOOTER)
    assert hashes == {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
    outputs = [out/'decode.mem',out/'uj11_decode_table.v']+[out/v/'uj11_decode_rom.v' for v in ('planes','bits')]
    (out/'inputs.json').write_text(json.dumps(dict(scope='CP36 opcode address logic; ISA entries unchanged',
        opcode_cases=65536, addressed_rows=len(seen), inputs_sha256=hashes,
        outputs_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs}),indent=2)+'\n')
    print(f'PASS CP36 compact dispatch: 65536 opcodes, {len(seen)} collision-free rows, one EBR')


if __name__=='__main__':
    main()
