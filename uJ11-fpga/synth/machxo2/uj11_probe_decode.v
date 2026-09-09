`timescale 1ns/1ps
// 16 stimulus FF + 10 observation bits before synthesis merging.
module uj11_probe_decode(input wire clk,reset,serial_in,output wire serial_out);
    reg [15:0] stimulus;
    reg [9:0] observe;
    wire [9:0] entry;
    uj11_decode decode(.ir(stimulus),.entry(entry));
    always @(posedge clk) begin
        if(reset) begin stimulus<=0; observe<=0; end
        else begin stimulus<={stimulus[14:0],serial_in}; observe<=entry; end
    end
    assign serial_out=^observe;
endmodule
