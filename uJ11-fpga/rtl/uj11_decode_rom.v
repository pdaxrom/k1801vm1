`timescale 1ns/1ps
// Compress only opcode bits irrelevant to dispatch. Every instruction selects
// its exact entry in one EBR; low-byte system opcodes have their own 256 rows.
// All 65536 opcodes are collision-checked by the generator and vendor miter.
module uj11_decode_rom(input wire clk, enable,
    input wire [15:0] incoming, output wire [9:0] entry);
    function [9:0] opcode_index;
        input [15:0] op;
        reg memory_mode;
        begin
            memory_mode=|op[5:3];
            case(op[15:12])
                0,8: opcode_index=(|op[15:8]) ?
                    {2'b01,op[15],op[11:6],memory_mode} : {2'b11,op[7:0]};
                7:opcode_index={4'b1000,op[11:9],(|op[8:6]),op[5],memory_mode};
                15:opcode_index=10'h240;
                default:opcode_index={2'b00,op[15:12],(|op[11:9]),memory_mode,2'b0};
            endcase
        end
    endfunction
    wire [9:0] index=opcode_index(incoming);
    uj11_decode_table table_rom(.clk(clk),.enable(enable),.address(index),.data(entry[8:0]));
    assign entry[9]=0;
endmodule
