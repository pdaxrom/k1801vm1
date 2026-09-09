`timescale 1ns/1ps
module uj11_probe_fram_pf(input wire clk,reset,serial_in,output wire serial_out);
    uj11_probe_fram #(.MEMORY_MODE(2)) probe(.clk(clk),.reset(reset),.serial_in(serial_in),.serial_out(serial_out));
endmodule
