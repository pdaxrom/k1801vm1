`timescale 1ns/1ps
// CRC-CCITT 1021, high byte first. The linear form shares XOR terms explicitly.
module bk_crc16_word(input wire [15:0] previous,value,output wire [15:0] result);
    wire [15:0] bits = previous ^ value;
    assign result[0] = ^(bits & 16'h1911);
    assign result[1] = ^(bits & 16'h3222);
    assign result[2] = ^(bits & 16'h6444);
    assign result[3] = ^(bits & 16'hc888);
    assign result[4] = ^(bits & 16'h9110);
    assign result[5] = ^(bits & 16'h3b31);
    assign result[6] = ^(bits & 16'h7662);
    assign result[7] = ^(bits & 16'hecc4);
    assign result[8] = ^(bits & 16'hd988);
    assign result[9] = ^(bits & 16'hb310);
    assign result[10] = ^(bits & 16'h6620);
    assign result[11] = ^(bits & 16'hcc40);
    assign result[12] = ^(bits & 16'h8191);
    assign result[13] = ^(bits & 16'h0322);
    assign result[14] = ^(bits & 16'h0644);
    assign result[15] = ^(bits & 16'h0c88);
endmodule
