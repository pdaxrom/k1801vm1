`timescale 1ns/1ps
module uj11_probe_mmu_dp #(parameter integer SHARED=0)(
    input wire clk,reset,serial_in,output wire serial_out);
    reg [54:0] stimulus;
    reg [101:0] observe;
    wire [15:0] ra,rb,q,result,writeback,block_address;
    wire [3:0] nzvc;
    wire rf_write,length_error;
    uj11_mmu_dp_compare #(.SHARED(SHARED)) measured(.clk(clk),.reset(reset),
        .enable(stimulus[35]),.a(stimulus[3:0]),.b(stimulus[7:4]),
        .operation(stimulus[11:8]),.pair(stimulus[14:12]),.destination(stimulus[17:15]),
        .d(stimulus[33:18]),.carry(stimulus[34]),.byte_mode(stimulus[36]),
        .borrow(stimulus[37]),.check_length(stimulus[38]),.apr_data(stimulus[54:39]),
        .read_a(ra),.read_b(rb),.q(q),.result(result),.writeback(writeback),
        .nzvc(nzvc),.rf_write(rf_write),.block_address(block_address),.length_error(length_error));
    always @(posedge clk)begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin
            stimulus<={stimulus[53:0],serial_in};
            observe<={ra,rb,q,result,writeback,nzvc,rf_write,block_address,length_error};
        end
    end
    assign serial_out=^observe;
endmodule
