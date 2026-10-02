// Four shared output paths: carry chain, Boolean truth table, left, right.
// Shared word/byte ALU: 16 operations, carry input and NZVC result.
`timescale 1ns/1ps
module uj11_alu (
    input wire [15:0] a,b,input wire [3:0] operation,
    input wire carry,byte_mode,output wire [15:0] result,output wire [3:0] nzvc);
    wire subtract=operation[2];
    wire carry_in=(operation[0]&carry)^subtract;
    wire [15:0] arithmetic_b=b^{16{subtract}};
    wire [16:0] sum={1'b0,a}+{1'b0,arithmetic_b}+{16'b0,carry_in};
    wire carry8=sum[8]^a[8]^arithmetic_b[8];
    wire arithmetic=(operation[3:1]==1 || operation[3:1]==2);
    wire left=(operation==11 || operation==14);
    wire right=(operation==12 || operation==13 || operation==15);
    function [3:0] boolean_truth;
        input [3:0] op;
        begin case(op)
        0:boolean_truth=4'b1100;
        1:boolean_truth=4'b1010;
        6:boolean_truth=4'b1000;
        7:boolean_truth=4'b1110;
        8:boolean_truth=4'b0110;
        9:boolean_truth=4'b0100;
        10:boolean_truth=4'b0011;
        default:boolean_truth=0;
        endcase end
    endfunction
    wire [3:0] truth=boolean_truth(operation);
    wire [15:0] logic_value;
    genvar bit_index;
    generate for(bit_index=0;bit_index<16;bit_index=bit_index+1)begin: boolean_bit
        // Ternary Shannon form also preserves PASS with an uninitialized
        // unused RF input in four-state simulation (b | ~b would be X).
        assign logic_value[bit_index]=a[bit_index] ?
            (b[bit_index]?truth[3]:truth[2]) : (b[bit_index]?truth[1]:truth[0]);
    end endgenerate
    wire [15:0] left_value={a[14:0],carry && operation==14};
    wire right_sign=(operation==13 && a[15]) || (operation==15 && carry);
    wire right_byte=(operation==13 && a[7]) || (operation==15 && carry);
    wire [15:0] right_value={right_sign,a[15:9],byte_mode?right_byte:a[8],a[7:1]};
    assign result=arithmetic ? sum[15:0] : left ? left_value :
                  right ? right_value : logic_value;
    wire sa=byte_mode?a[7]:a[15];
    wire sb=byte_mode?arithmetic_b[7]:arithmetic_b[15];
    wire ss=byte_mode?sum[7]:sum[15];
    wire n=byte_mode?result[7]:result[15];
    wire c=(arithmetic && ((byte_mode?carry8:sum[16])^subtract)) ||
           (left && sa) || (right && a[0]) || operation==10;
    wire v=(left||right)?n^c:(arithmetic && !(sa^sb) && (sa^ss));
    assign nzvc={n,(result[7:0]==0 && (byte_mode || result[15:8]==0)),v,c};
endmodule
