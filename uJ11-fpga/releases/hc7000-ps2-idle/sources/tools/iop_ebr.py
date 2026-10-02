"""MachXO2 512x18 simple dual-port EBR, 16 payload bits and byte enables.

Port A writes the two 9-bit lanes from DIA/DIB; port B reads DOA/DOB.
ADA[1:0] are the byte enables, ADA/ADB[12:4] the word address. Two parity
bits stay zero. Each 18-bit initialized word occupies one 20-bit INITVAL slot.
"""
def block(instance, address, write_data, write_enable, data, words=None):
    lines=['PDPW8KC #(.DATA_WIDTH_W(18),.DATA_WIDTH_R(18),.REGMODE("NOREG"),',
        ' .CSDECODE_W("0b000"),.CSDECODE_R("0b000"),',
        ' .GSR("DISABLED"),.RESETMODE("SYNC"),.ASYNC_RESET_RELEASE("SYNC"),.INIT_DATA("STATIC")'+(',' if words is not None else '')]
    if words is not None:
        assert len(words)==512
        for b in range(32):
            v=0
            for n in range(16):
                w=words[16*b+n]
                v|=((w&255)|((w>>8)<<9))<<(20*n)
            lines.append(f' .INITVAL_{b:02X}("0x{v:080X}"){"," if b<31 else ""}')
    lines.append(f') {instance} (')
    ports=[]
    for i in range(18):
        ports.append((f'DI{i}',f'{write_data}[{i if i<9 else i-1}]' if i%9<8 else "1'b0"))
        # PDPW8KC's 18-bit read exposes the low 9-bit lane on DO[17:9],
        # the high lane on DO[8:0] (see Lattice PDPW8KC.v -> DP8KC).
        bit=i+8 if i<9 else i-9
        ports.append((f'DO{i}',f'{data}[{bit}]' if i%9<8 else ''))
    ports += [(f'ADW{i}',f'{address}[{i}]') for i in range(9)]
    ports += [(f'ADR{i}',f'{address}[{i-4}]' if i>=4 else "1'b0") for i in range(13)]
    ports += [('BE0',f'{write_enable}[0]'),('BE1',f'{write_enable}[1]'),
              ('CEW',f'enable && (|{write_enable})'),('CER',f'enable && !(|{write_enable})'),
              ('OCER',"1'b1"),('CLKW','clk'),('CLKR','clk'),('RST',"1'b0")]
    ports += [(f'CS{side}{i}',"1'b0") for side in 'WR' for i in range(3)]
    lines += [f' .{n}({v}){"," if i<len(ports)-1 else ""}' for i,(n,v) in enumerate(ports)]
    return '\n'.join(lines+[');'])

def sector_ram():
    return '\n'.join(['// Generated 512-byte sector RAM; one 512x18 EBR.',
        'module uj11_sector_ram(input wire clk, write, input wire [7:0] address,',
        ' input wire [15:0] write_data,output wire [15:0] data);',
        "wire enable=1'b1;wire [1:0] we={2{write}};wire [8:0] a={1'b0,address};",
        '`ifdef SYNTHESIS','`define UJ11_IOP_EBR',
        '`elsif UJ11_IOP_VENDOR_RAM','`define UJ11_IOP_EBR','`endif','`ifdef UJ11_IOP_EBR',
        block('ram','a','write_data','we','data'),
        '`else','reg [15:0] words[0:255];reg [15:0] value;',
        'always @(posedge clk) if(write)words[address]<=write_data;else value<=words[address];',
        'assign data=value;','`endif','`undef UJ11_IOP_EBR','endmodule',''])
