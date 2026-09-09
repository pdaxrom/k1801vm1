`timescale 1ns/1ps
module uj11_probe_fram_seq(input wire clk,reset,serial_in,output wire serial_out);
    uj11_probe_fram #(.MEMORY_MODE(1)) probe(.clk(clk),.reset(reset),.serial_in(serial_in),.serial_out(serial_out));
endmodule
