`timescale 1ns/1ps
// Identical interfaces and observation masks for the area experiments.
// SHARED=0 dedicated, 1 share relocation and length, 2 share relocation only.
// Scope: CPU datapath + MMU arithmetic, not APR/MMR, bus or full translation.
module uj11_mmu_dp_compare #(parameter integer SHARED=0)(
    input wire clk,reset,enable,carry,byte_mode,borrow,check_length,
    input wire [3:0] a,b,operation,input wire [2:0] pair,destination,
    input wire [15:0] d,apr_data,
    output wire [15:0] read_a,read_b,q,result,writeback,block_address,
    output wire [3:0] nzvc,output wire rf_write,length_error);
    wire [15:0] raw_result,raw_writeback;
    wire [3:0] raw_flags;
    wire [7:0] delta={1'b0,read_a[12:6]}-{1'b0,apr_data[14:8]};
    wire dedicated_length_error=borrow && check_length &&
        (apr_data[3] ? delta[7] : (!delta[7] && |delta[6:0]));
    generate if(SHARED!=0)begin: shared
        uj11_datapath_borrow dp(.clk(clk),.reset(reset),
            .enable(enable && !(SHARED==2 && borrow && check_length)),
            .a(a),.b(b),.operation(operation),.pair(pair),.destination(destination),.d(d),
            .carry(carry),.byte_mode(byte_mode),.borrow(borrow && (SHARED!=2 || !check_length)),
            .check_length(SHARED==2 ? 1'b0 : check_length),
            .apr_data(apr_data),.read_a(read_a),.read_b(read_b),.result(raw_result),.q(q),
            .nzvc(raw_flags),.rf_write(rf_write),.writeback(raw_writeback));
        assign block_address=(borrow && !check_length) ? raw_result : 16'b0;
        assign length_error=SHARED==2 ? dedicated_length_error : borrow && check_length &&
            (apr_data[3] ? raw_flags[0] : (!raw_flags[0] && !raw_flags[2]));
    end else begin: dedicated
        uj11_datapath dp(.clk(clk),.reset(reset),.enable(enable && !borrow),
            .a(a),.b(b),.operation(operation),.pair(pair),.destination(destination),.d(d),
            .carry(carry),.byte_mode(byte_mode),.read_a(read_a),.read_b(read_b),
            .result(raw_result),.q(q),.nzvc(raw_flags),.rf_write(rf_write),.writeback(raw_writeback));
        // Same independent block add / shared length subtract as CP32.
        wire [15:0] sum=apr_data+{9'b0,read_a[12:6]};
        assign block_address=(borrow && !check_length) ? sum : 16'b0;
        assign length_error=dedicated_length_error;
    end endgenerate
    // Normal ALU outputs are not consumed while borrowed; neither variant
    // is charged for preserving them. RF ports and Q remain visible/valid.
    assign result=borrow ? 16'b0 : raw_result;
    assign writeback=borrow ? 16'b0 : raw_writeback;
    assign nzvc=borrow ? 4'b0 : raw_flags;
endmodule
