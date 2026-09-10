#!/usr/bin/env python3
"""Generate synchronous 1024x9 opcode dispatch table from the readable decoder."""
from pathlib import Path
import subprocess
from make_ebr import generate
ROOT=Path(__file__).resolve().parents[1]

def main():
    out=ROOT/'build/decode-rom';out.mkdir(parents=True,exist_ok=True)
    (out/'dump.v').write_text('''module dump;
reg [15:0] ir; wire [9:0] entry; integer i;
uj11_decode dut(ir,entry);
initial begin for(i=0;i<65536;i=i+1)begin ir=i[15:0];#1;$display("%03x",entry);end $finish;end
endmodule
''')
    subprocess.run(['iverilog','-g2012','-s','dump','-o',str(out/'dump'),str(out/'dump.v'),str(ROOT/'rtl/uj11_decode.v')],check=True)
    rows=subprocess.check_output(['vvp',str(out/'dump')],text=True).splitlines()
    truth=[int(s,16) for s in rows if len(s)==3]
    assert len(truth)==65536 and max(truth)<512
    words=[0x42]*1024; seen={}
    for opcode,entry in enumerate(truth):
        group=opcode>>12; memory_mode=int(bool(opcode&0o70))
        if group in (0,8):
            index=(0x100 | ((opcode>>15)<<7) | (((opcode>>6)&63)<<1) | memory_mode) if opcode>>8 else (0x300 | (opcode&255))
        elif group==7:
            index=0x200 | (((opcode>>9)&7)<<3) | (int(bool(opcode&0o700))<<2) | (((opcode>>5)&1)<<1) | memory_mode
        elif group==15:index=0x240
        else:index=(group<<4) | (int(bool(opcode&0o7000))<<3) | (memory_mode<<2)
        assert index not in seen or seen[index]==entry,(oct(opcode),index,entry,seen.get(index))
        seen[index]=entry;words[index]=entry
    print(f'{len(seen)} addressed rows; exhaustive opcode collision check passed')
    dest=ROOT/'microcode/generated';dest.mkdir(exist_ok=True)
    (dest/'decode.mem').write_text(''.join(f'{w:03x}\n' for w in words))
    # Reuse the vendor-tested 1024x9 packing; retain just the first lane.
    source=generate(words).split('    DP8KC #(',2)
    header=source[0].replace('uj11_rom','uj11_decode_table').replace('[35:0]','[8:0]')
    vendor='    DP8KC #('+source[1]
    code=header+'''`ifdef SYNTHESIS
`define UJ11_DECODE_EBR
`elsif UJ11_VENDOR_ROM
`define UJ11_DECODE_EBR
`endif
`ifdef UJ11_DECODE_EBR
'''+vendor+'''`else
    reg [8:0] words[0:1023]; reg [8:0] value;
    initial $readmemh("microcode/generated/decode.mem",words);
    always @(posedge clk) if(enable) value<=words[address];
    assign data=value;
`endif
`undef UJ11_DECODE_EBR
endmodule
'''
    (dest/'uj11_decode_table.v').write_text(code)
    print('Opcode table: 1024 x 9, one EBR')
if __name__=='__main__':main()
