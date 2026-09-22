"""Generate the two byte-wide MachXO2 firmware EBRs."""
from make_ebr import lane_initvals
def firmware_rom(words):
    """Two 1024x9 EBRs, one byte per chip; no 16-bit output bank mux."""
    assert len(words) == 1024
    lines = ['// Production 1024x16 firmware, two byte-wide EBRs.',
             'module uj11_firmware_rom(input wire clk, enable,',
             ' input wire [9:0] address, output wire [15:0] data,',
             ' input wire [1:0] write_enable, input wire [15:0] write_data);',
             "wire [1:0] we=write_enable & {2{address[9:4]==6'b001111}};",
             '`ifdef SYNTHESIS', '`define UJ11_FW_EBR',
             '`elsif UJ11_VENDOR_ROM', '`define UJ11_FW_EBR', '`endif',
             '`ifdef UJ11_FW_EBR']
    for lane in range(2):
        values = [(w >> (8*lane)) & 255 for w in words]
        lines += ['DP8KC #(', ' .DATA_WIDTH_A(9), .DATA_WIDTH_B(9),',
                  ' .REGMODE_A("NOREG"), .REGMODE_B("NOREG"),',
                  ' .CSDECODE_A("0b000"), .CSDECODE_B("0b000"),',
                  ' .WRITEMODE_A("NORMAL"), .WRITEMODE_B("NORMAL"),',
                  ' .GSR("DISABLED"), .RESETMODE("SYNC"),',
                  ' .ASYNC_RESET_RELEASE("SYNC"), .INIT_DATA("STATIC"),']
        lines += [f' .INITVAL_{i:02X}("0x{v:080X}"){"," if i<31 else ""}'
                  for i, v in enumerate(lane_initvals(values, 0))]
        lines.append(f') fw{lane} (')
        ports = []
        for side in 'AB':
            ports += [(f'DI{side}{i}', f'write_data[{8*lane+i}]' if side=='A' and i<8 else "1'b0") for i in range(9)]
            ports += [(f'AD{side}{i}', f'address[{i-3}]' if i>=3 else ("1'b1" if i==0 else "1'b0")) for i in range(13)]
            ports += [(f'CE{side}', 'enable' if side=='A' else "1'b0"),
                      (f'OCE{side}', 'enable' if side=='A' else "1'b0"),
                      (f'CLK{side}', 'clk'), (f'WE{side}', f'we[{lane}]' if side=='A' else "1'b0"),
                      (f'RST{side}', "1'b0")]
            ports += [(f'CS{side}{i}', "1'b0") for i in range(3)]
            ports += [(f'DO{side}{i}', f'data[{8*lane+i}]' if side=='A' and i<8 else '') for i in range(9)]
        lines += [f' .{n}({v}){"," if i<len(ports)-1 else ""}' for i,(n,v) in enumerate(ports)]
        lines.append(');')
    lines += ['`else', 'reg [15:0] words[0:1023]; reg [15:0] value;',
              'initial $readmemh("build/hardware/firmware.mem",words);',
              'always @(posedge clk) if(enable) begin',
              ' value<=words[address];',
              ' if(we[0])words[address][7:0]<=write_data[7:0];',
              ' if(we[1])words[address][15:8]<=write_data[15:8];',
              'end', 'assign data=value;', '`endif', '`undef UJ11_FW_EBR', 'endmodule', '']
    return '\n'.join(lines)
