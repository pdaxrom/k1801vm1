`timescale 1ns/1ps
// Microcode-controlled wide scratch datapath. It does not decode FP opcodes,
// access guest memory, update an AC/FPS, or choose an arithmetic algorithm.
// The only adder is 16 bits, reused over four words. uJ11 microcode performs
// alignment, normalization, multiplication/division loops and exception policy.
// Internal words: 0..3 X, 4..7 Y, 8..15 Z (DEC high-word first), 16 command/
// status, 17 EX, 18 EY, 19 signs, 20 precision (0=24,1=56 significant bits).
module uj11_mmu_fp_datapath(
    input wire clk,reset,request,writing,
    input wire [4:0] address,
    input wire [15:0] write_data,
    output reg [15:0] read_data,
    output reg ready
);
    localparam ZERO_X=1,ZERO_Y=2,ZERO_Z=3,SWAP=4,Y_TO_X=5,X_TO_Y=6,
        UNPACK_X=7,UNPACK_Y=8,PACK_X=9,SHL_X=10,SHR_X=11,JAM_Y=12,
        ADD=13,SUB=14,ROUND=15,ADD_HIGH=16,SHR_Z=17,PRODUCT=18,
        MULTIPLIER=19,SHL_Z=20,SET_Z0=21,Z_TO_X=22,NEGATE=23,
        SEXT16=24,SEXT32=25,TRUNC59=26,AND=27,BIC=28,SHL_Y=29,SHR_Y=30,SET_Y0=31,X_TO_Z=32;
    reg [63:0] x,y;
    reg [127:0] z;
    reg [15:0] ex,ey;
    reg sx,sy,precision,zc,seen,busy,carry;
    reg [1:0] index;
    reg [4:0] operation;
    wire [15:0] ax=operation==ADD_HIGH ? z[64+16*index+:16] : x[16*index+:16];
    wire [63:0] rounding=precision ? 64'h40 : 64'h4000000000;
    wire [15:0] ay=operation==ROUND ? rounding[16*index+:16] :
        operation==ADD_HIGH ? x[16*index+:16] : y[16*index+:16];
    wire [15:0] lhs=operation==NEGATE ? ~ax : ax;
    wire [15:0] rhs=operation==NEGATE ? 16'b0 : operation==SUB ? ~ay : ay;
    wire [16:0] sum={1'b0,lhs}+{1'b0,rhs}+{16'b0,carry};
    wire [15:0] result=operation==AND ? ax & ay : operation==BIC ? ax & ~ay : sum[15:0];
    // Combinational diagnostics are sampled by the ordinary internal READ.
    always @* begin
        case(address[4:2])
            0:read_data=x[63-16*address[1:0]-:16];
            1:read_data=y[63-16*address[1:0]-:16];
            2:read_data=z[127-16*address[1:0]-:16];
            3:read_data=z[63-16*address[1:0]-:16];
            default:case(address)
                16:read_data={7'b0,z[0],y[0],x[0],x[62],x[63],x==y,x<y,y==0,x==0};
                17:read_data=ex;
                18:read_data=ey;
                19:read_data={14'b0,sy,sx};
                20:read_data={15'b0,precision};
                default:read_data=0;
            endcase
        endcase
    end
    always @(posedge clk)begin
        if(reset)begin
            x<=0;y<=0;z<=0;ex<=0;ey<=0;sx<=0;sy<=0;precision<=0;
            zc<=0;seen<=0;busy<=0;carry<=0;index<=0;operation<=0;ready<=0;
        end else if(busy)begin
            if(operation==ADD_HIGH)z[64+16*index+:16]<=result;
            else x[16*index+:16]<=result;
            carry<=sum[16];index<=index+1'b1;
            if(index==3)begin
                if(operation==ADD_HIGH)zc<=sum[16];
                busy<=0;ready<=1;
            end
        end else if(!request)begin seen<=0;ready<=0;end
        else if(!seen)begin
            seen<=1;ready<=1;
            if(writing)case(address[4:2])
                0:x[63-16*address[1:0]-:16]<=write_data;
                1:y[63-16*address[1:0]-:16]<=write_data;
                2:z[127-16*address[1:0]-:16]<=write_data;
                3:z[63-16*address[1:0]-:16]<=write_data;
                default:case(address)
                    17:ex<=write_data;
                    18:ey<=write_data;
                    19:begin sx<=write_data[0];sy<=write_data[1];end
                    20:precision<=write_data[0];
                    16:case(write_data[5:0])
                        ZERO_X:x<=0;
                        ZERO_Y:y<=0;
                        ZERO_Z:begin z<=0;zc<=0;end
                        SWAP:begin x<=y;y<=x;ex<=ey;ey<=ex;sx<=sy;sy<=sx;end
                        Y_TO_X:begin x<=y;ex<=ey;sx<=sy;end
                        X_TO_Y:begin y<=x;ey<=ex;sy<=sx;end
                        UNPACK_X:begin
                            ex<={8'b0,x[62:55]};sx<=x[63] && |x[62:55];
                            x<=|x[62:55] ? {2'b01,x[54:32],precision ? x[31:0] : 32'b0,7'b0} : 64'b0;
                        end
                        UNPACK_Y:begin
                            ey<={8'b0,y[62:55]};sy<=y[63] && |y[62:55];
                            y<=|y[62:55] ? {2'b01,y[54:32],precision ? y[31:0] : 32'b0,7'b0} : 64'b0;
                        end
                        PACK_X:x<=x==0 ? 64'b0 : {sx,ex[7:0],x[61:39],precision ? x[38:7] : 32'b0};
                        SHL_X:x<={x[62:0],1'b0};
                        SHR_X:x<={1'b0,x[63:1]};
                        JAM_Y:y<={1'b0,y[63:2],y[1] | y[0]};
                        SHL_Y:y<={y[62:0],1'b0};
                        SHR_Y:y<={1'b0,y[63:1]};
                        SET_Y0:y[0]<=1;
                        ADD,SUB,ROUND,ADD_HIGH,NEGATE,AND,BIC:begin
                            operation<=write_data[4:0];index<=0;busy<=1;ready<=0;
                            carry<=write_data[4:0]==SUB || write_data[4:0]==NEGATE;
                        end
                        SHR_Z:begin z<={zc,z[127:1]};zc<=0;end
                        PRODUCT:x<=z[126:63];
                        MULTIPLIER:begin z<={64'b0,y};zc<=0;end
                        SHL_Z:z<={z[126:0],1'b0};
                        SET_Z0:z[0]<=1;
                        Z_TO_X:x<=z[63:0];
                        X_TO_Z:z[63:0]<=x;
                        SEXT16:x<={{48{x[15]}},x[15:0]};
                        SEXT32:x<={{32{x[31]}},x[31:0]};
                        TRUNC59:x[3:0]<=0;
                        default:begin end
                    endcase
                    default:begin end
                endcase
            endcase
        end
    end
    wire unused_write_data=^write_data[15:6];
endmodule
