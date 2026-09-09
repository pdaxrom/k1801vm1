`timescale 1ns/1ps
// 37 stimulus FF + 85 raw observation bits before synthesis merging.
// XOR is AFTER observation registers, outside the internal timed datapath.
module uj11_probe_datapath(input wire clk,reset,serial_in,output wire serial_out);
    reg [36:0] stimulus;
    reg [84:0] observe;
    wire [15:0] ra,rb,result,q,writeback;
    wire [3:0] nzvc;
    wire write_enable;
    uj11_datapath dp(.clk(clk),.reset(reset),.enable(stimulus[35]),
        .a(stimulus[3:0]),.b(stimulus[7:4]),.operation(stimulus[11:8]),
        .pair(stimulus[14:12]),.destination(stimulus[17:15]),
        .d(stimulus[33:18]),.carry(stimulus[34]),.byte_mode(stimulus[36]),
        .read_a(ra),.read_b(rb),.result(result),.q(q),.nzvc(nzvc),
        .rf_write(write_enable),.writeback(writeback));
    always @(posedge clk) begin
        if(reset) begin stimulus<=0; observe<=0; end
        else begin
            stimulus <= {stimulus[35:0],serial_in};
            observe <= {ra,rb,q,result,writeback,nzvc,write_enable};
        end
    end
    assign serial_out=^observe;
endmodule
