`timescale 1ns/1ps
// CP36: every opcode and enable hold is checked against the old dispatch.
module uj11_decode_rom(input wire clk, enable,
    input wire [15:0] incoming, output wire [9:0] entry);
    wire [15:0] op = incoming;
    wire system_low = ~|op[15:8];
    wire single_group = ~|op[14:12];
    wire eis_group = op[15:12]==4'h7;
    wire memory_mode = |op[5:3];
    wire [9:0] index;
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
    uj11_decode_table table_rom(.clk(clk),.enable(enable),.address(index),.data(entry[8:0]));
    assign entry[9]=0;
endmodule
