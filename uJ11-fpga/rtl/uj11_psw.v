`timescale 1ns/1ps
module uj11_psw (
    input wire clk, reset, enable,
    input wire [1:0] update,
    input wire [3:0] nzvc,
    input wire [15:0] value,
    output reg [15:0] psw
);
    always @(posedge clk) begin
        if (reset) psw <= 16'o000340; // J-11 boot: kernel, IPL7, T=0
        else if (enable) begin
            case (update)
                2'd1: psw[3:1] <= nzvc[3:1];
                2'd2: psw[3:0] <= nzvc;
                2'd3: psw <= value;
                default: psw <= psw;
            endcase
        end
    end
endmodule
