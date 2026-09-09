// CP27 ALU from commit 0f1047e; module renamed for formal equivalence.
`timescale 1ns/1ps
// Encoding v1. A single 17-bit carry chain serves ADD/ADC/SUB/SBC.
// Subtraction reports PDP-11 borrow, not the inverted carry-chain output.
module uj11_alu_cp27 (
    input wire [15:0] a, b,
    input wire [3:0] operation,
    input wire carry, byte_mode,
    output reg [15:0] result,
    output wire [3:0] nzvc
);
    wire subtract = operation[2]; // relevant only for arithmetic opcodes 2..5
    wire carry_in = subtract ? ~(operation[0] & carry) : (operation[0] & carry);
    wire [15:0] arithmetic_b = b ^ {16{subtract}};
    wire [16:0] sum = {1'b0,a} + {1'b0,arithmetic_b} + {16'b0,carry_in};
    // Carry into bit 8 is recovered from sum[8], retaining one 17-bit adder.
    wire carry8 = sum[8] ^ a[8] ^ arithmetic_b[8];
    wire sign_a = byte_mode ? a[7] : a[15];
    wire sign_b = byte_mode ? arithmetic_b[7] : arithmetic_b[15];
    wire sign_sum = byte_mode ? sum[7] : sum[15];
    wire negative = byte_mode ? result[7] : result[15];
    reg v, c;
    always @* begin
        result = a;
        v = 0;
        c = 0;
        case (operation)
            4'd0: result = a;
            4'd1: result = b;
            4'd2, 4'd3, 4'd4, 4'd5: begin
                result = sum[15:0];
                c = (byte_mode ? carry8 : sum[16]) ^ subtract;
                v = ~(sign_a ^ sign_b) & (sign_a ^ sign_sum);
            end
            4'd6: result = a & b;
            4'd7: result = a | b;
            4'd8: result = a ^ b;
            4'd9: result = a & ~b;
            4'd10: begin result = ~a; c = 1; end
            4'd11: begin result = {a[14:0],1'b0}; c=sign_a; end
            4'd12: begin
                result = {1'b0,a[15:1]};
                if(byte_mode)result[7]=0;
                c=a[0];
            end
            4'd13: begin
                result = {a[15],a[15:1]};
                if(byte_mode)result[7]=a[7];
                c=a[0];
            end
            4'd14: begin result = {a[14:0],carry}; c=sign_a; end
            4'd15: begin
                result = {carry,a[15:1]};
                if(byte_mode)result[7]=carry;
                c=a[0];
            end
        endcase
    end
    assign nzvc = {negative, (result[7:0]==0 && (byte_mode || result[15:8]==0)),
                   operation>=4'd11 ? negative^c : v, c};
endmodule
