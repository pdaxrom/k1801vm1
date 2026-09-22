// Portable synchronous-ROM model. Diamond compiles the generated DP8KC
// implementation of this module instead; never compile both definitions.
`timescale 1ns/1ps
module uj11_rom #(parameter IMAGE = "build/hardware/m0.mem") (
    input wire clk, enable,
    input wire [9:0] address,
    output reg [35:0] data
);
    reg [35:0] words [0:1023];
    initial $readmemh(IMAGE, words);
    always @(posedge clk)
        if (enable) data <= words[address];
endmodule
