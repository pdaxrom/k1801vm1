`timescale 1ns/1ps
// Portable simulation only; synthesis uses the real MachXO2 ODDRXE primitive.
// Its D1 input is sampled on posedge, then sent on the following negedge.
module ODDRXE(input wire D0,D1,RST,SCLK,output reg Q);
    reg next_low;
    initial begin Q=0;next_low=0;end
    always @(posedge SCLK or posedge RST)
        if(RST)next_low<=0;else next_low<=D1;
    always @(posedge SCLK or negedge SCLK or posedge RST)
        if(RST)Q<=0;
        else if(SCLK)Q<=D0;
        else Q<=next_low;
endmodule
