`timescale 1ns/1ps
module uj11_probe_mmu_dp_relocation(input wire clk,reset,serial_in,output wire serial_out);
    uj11_probe_mmu_dp #(.SHARED(2)) probe(.*);
endmodule
