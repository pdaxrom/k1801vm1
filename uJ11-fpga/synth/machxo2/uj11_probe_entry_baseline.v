`timescale 1ns/1ps
module uj11_probe_entry_baseline(input wire clk,reset,serial_in,output wire serial_out);
    uj11_probe_entry_core probe(.*);
endmodule
