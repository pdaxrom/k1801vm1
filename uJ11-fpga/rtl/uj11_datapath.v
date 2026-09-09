`timescale 1ns/1ps
// Shared Am2901-like operand pair, RF writeback and Q. No opcode decode.
module uj11_datapath (
    input wire clk, reset, enable,
    input wire [3:0] a, b, operation,
    input wire [2:0] pair, destination,
    input wire [15:0] d,
    input wire carry, byte_mode,
    output wire [15:0] read_a, read_b, result,
    output reg [15:0] q,
    output wire [3:0] nzvc,
    output wire rf_write,
    output wire [15:0] writeback
);
    // Decode once, then parallel masked buses. Avoid cascaded priority muxes.
    wire [15:0] lhs = (read_a & {16{pair==0 || pair==1 || pair==2}}) |
                      (d & {16{pair==3 || pair==5 || pair==6}}) |
                      (read_b & {16{pair==7}});
    wire [15:0] rhs = (read_b & {16{pair==0 || pair==3 || pair==4}}) |
                      (q & {16{pair==1 || pair==5}}) |
                      (d & {16{pair==2}}) | (read_a & {16{pair==6 || pair==7}});
    uj11_alu alu(.a(lhs),.b(rhs),.operation(operation),.carry(carry),.byte_mode(byte_mode),
                 .result(result),.nzvc(nzvc));
    // Operand write preserves the upper byte; MOVB sign-extends it. The full
    // merged value also feeds PC redirect/debug, so no separate RF byte port.
    wire keep_high = byte_mode && destination==3'd5;
    wire sign_high = byte_mode && destination==3'd6;
    wire [15:0] ordinary_writeback = {
        (read_b[15:8] & {8{keep_high}}) | ({8{result[7]}} & {8{sign_high}}) |
        (result[15:8] & {8{!keep_high && !sign_high}}), result[7:0]};
    assign writeback = ({result[14:0],q[15]} & {16{destination==3'd3}}) |
                      ({result[15],result[15:1]} & {16{destination==3'd4}}) |
                      (ordinary_writeback & {16{destination!=3'd3 && destination!=3'd4}});
    assign rf_write = enable && !reset &&
                      (destination==3'd1 || destination==3'd3 || destination==3'd4 ||
                       destination==3'd5 || destination==3'd6);
    uj11_regfile rf(.clk(clk),.write_enable(rf_write),.a(a),.b(b),
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
