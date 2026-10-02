`timescale 1ns/1ps
// One EBR for FPS/FEC/FEA, six 64-bit ACs and scratch words. Internal only.
// Word order in an AC is DEC high word first. Reset clears all 32 words.
module uj11_mmu_fp_state(
    input wire clk,reset,request,writing,
    input wire [4:0] address,
    input wire [15:0] write_data,
    output wire [15:0] read_data,
    output reg ready,initialized
);
    reg [4:0] clear_address;
    reg seen;
    wire enable=!reset && (!initialized || (request && !seen));
    wire write_enable=!initialized || writing;
    wire [15:0] masked=address==0 ? write_data & 16'o147757 : write_data;
    uj11_mmu_apr_ram storage(.clk(clk),.enable(enable),
        .address({2'b0,initialized ? address : clear_address}),
        .write_enable({2{write_enable}}),.write_data(initialized ? masked : 16'b0),.read_data(read_data));
    always @(posedge clk)begin
        if(reset)begin ready<=0;initialized<=0;clear_address<=0;seen<=0;end
        else if(!initialized)begin
            if(clear_address==31)initialized<=1;
            else clear_address<=clear_address+1'b1;
        end else if(!request)begin seen<=0;ready<=0;end
        else if(!seen)begin seen<=1;ready<=1;end
    end
endmodule
