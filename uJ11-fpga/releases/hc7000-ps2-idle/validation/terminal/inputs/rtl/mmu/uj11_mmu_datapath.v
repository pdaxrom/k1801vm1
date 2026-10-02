`timescale 1ns/1ps
// Shared Am2901-like operand pair, RF writeback and Q. No opcode decode.
module uj11_mmu_datapath (
    input wire clk, reset, enable,
    input wire [3:0] a, b, operation,
    input wire [2:0] pair, destination,
    input wire [15:0] d,psw,
    input wire previous,pipeline_enabled,
    input wire defer_write,commit_deferred,discard_deferred,
    output wire initialized,
    input wire debug_write,
    input wire [4:0] debug_address,
    input wire [15:0] debug_data,
    output wire [15:0] debug_read_data,pc,
    input wire carry, byte_mode,
    output wire [15:0] read_a, read_b, result,
    output reg [15:0] q,
    output wire [3:0] nzvc,
    output wire rf_write,
    output wire [15:0] writeback
);
    wire [1:0] lhs_select = {
        pair[2] && (pair[1]==pair[0]),
        (pair[1] && pair[0]) || (pair[2] && (pair[1] || pair[0]))};
    wire [1:0] rhs_select = {
        pair[1] && (pair[2] || !pair[0]),
        (pair[0] && !pair[1]) || (pair[2] && pair[1])};
    // LHS 00=A,01=D,10=0,11=B; RHS 00=B,01=Q,10=D,11=A.
    wire [15:0] lhs = lhs_select[1] ? (lhs_select[0] ? read_b : 16'b0) :
                                    (lhs_select[0] ? d : read_a);
    wire [15:0] rhs = rhs_select[1] ? (rhs_select[0] ? read_a : d) :
                                    (rhs_select[0] ? q : read_b);
    uj11_alu alu(.a(lhs),.b(rhs),.operation(operation),.carry(carry),.byte_mode(byte_mode),
                 .result(result),.nzvc(nzvc));
    // Operand write preserves the upper byte; MOVB sign-extends it. The full
    // merged value also feeds PC redirect/debug, so no separate RF byte port.
    wire keep_high = byte_mode && destination==3'd5;
    wire sign_high = byte_mode && destination==3'd6;
    // Resolve long shifts first. Byte merge only applies to destinations
    // 5/6, so these selects are disjoint without an extra ordinary-data mask.
    wire [15:0] shifted = destination==3'd3 ? {result[14:0],q[15]} :
                          destination==3'd4 ? {result[15],result[15:1]} : result;
    assign writeback = {keep_high ? read_b[15:8] :
                        sign_high ? {8{result[7]}} : shifted[15:8], shifted[7:0]};
    assign rf_write = enable && !reset &&
                      (destination==3'd1 || destination==3'd3 || destination==3'd4 ||
                       destination==3'd5 || destination==3'd6);
    uj11_mmu_regfile rf(.clk(clk),.reset(reset),.write_enable(rf_write),.a(a),.b(b),
        .psw(psw),.previous(previous),.pipeline_enabled(pipeline_enabled),.initialized(initialized),
        .defer_write(defer_write),.commit_deferred(commit_deferred),.discard_deferred(discard_deferred),
        .debug_write(debug_write),.debug_address(debug_address),
        .debug_data(debug_data),.debug_read_data(debug_read_data),.pc(pc),
                    .write_data(writeback),.read_a(read_a),.read_b(read_b));
    always @(posedge clk) begin
        if (reset) q <= 0;
        else if (enable) begin
            case (destination)
                3'd2: q <= result;
                3'd3: q <= {q[14:0],1'b0};
                3'd4: q <= {result[0],q[15:1]};
                default: q <= q;
            endcase
        end
    end
endmodule
