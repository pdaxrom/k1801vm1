`timescale 1ns/1ps
// CP44 MMR0 software controls only, DEC 4.7.1. Writable flags do not trap.
// Abort/page status, MMR1/MMR2 and freeze of hardware metadata are not wired.
module uj11_mmr0_control (
    input wire clk, reset, request, writing,
    input wire [1:0] byte_enable,
    input wire [15:0] write_data,
    output wire [15:0] value,
    output reg enabled, ready
);
    // The remaining byte-lane bits are intentionally not writable.
    wire unused_write_data = ^write_data[12:1];
    reg [2:0] software_flags;
    assign value={software_flags,12'b0,enabled};
    always @(posedge clk) begin
        if(reset)begin enabled<=0;software_flags<=0;ready<=0;end
        else begin
            ready<=request;
            if(request && !ready && writing)begin
                if(byte_enable[0])enabled<=write_data[0];
                if(byte_enable[1])software_flags<=write_data[15:13];
            end
        end
    end
endmodule
