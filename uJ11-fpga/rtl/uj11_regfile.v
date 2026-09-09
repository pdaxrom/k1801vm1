`timescale 1ns/1ps
// Two asynchronous reads; one synchronous write at B. No reset tree:
// the reset microprogram initializes all 16 words through the ordinary port.
module uj11_regfile (
    input wire clk, write_enable,
    input wire [3:0] a, b,
    input wire [15:0] write_data,
    output wire [15:0] read_a, read_b
);
    reg [15:0] words [0:15] /* synthesis syn_ramstyle = "distributed" */;
    assign read_a = words[a];
    assign read_b = words[b];
    always @(posedge clk)
        if (write_enable) words[b] <= write_data;
endmodule
