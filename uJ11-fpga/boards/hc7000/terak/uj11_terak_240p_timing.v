`timescale 1ns/1ps
// 262 complete lines per progressive frame, exactly 60 frames/s at 64 MHz.
// This NTSC-like 240p diagnostic keeps every frame in the same scan phase.
module uj11_terak_240p_timing(
    input wire clk,reset,
    output reg [11:0] x,
    output reg [10:0] half_line,
    output wire [9:0] line_number,
    output wire line_start,frame_start,field_start,field,
    output wire sync,active_line,active,prefetch,
    output wire [7:0] row,next_row
);
    reg [10:0] half_x,half_last;
    reg [8:0] fraction;
    wire [9:0] next_fraction={1'b0,fraction}+10'd245;
    assign line_number=half_line[10:1];
    assign line_start=x==0;
    assign frame_start=half_line==0 && half_x==0;
    assign field_start=frame_start;
    assign field=1'b0;
    // Pulse mode changes only at half-line boundaries. Registering the mode
    // removes the vertical range-decode chain from the 64 MHz DAC path.
    reg broad,equalize;
    // 4.703 us line sync, 2.297 us equalizing, and one line-sync-width gap
    // between broad pulses. Colour subcarrier/burst is unnecessary for Terak.
    assign sync=broad ? half_x<half_last-11'd300 : equalize ? half_x<147 : x<301;
    assign active_line=line_number>=18 && line_number<258;
    assign row=line_number-18;
    assign active=active_line && x>=768 && x<3968;
    assign prefetch=line_number>=17 && line_number<257;
    assign next_row=line_number-17;
    always @(posedge clk)begin
        if(reset)begin
            x<=0;half_x<=0;half_line<=0;half_last<=2034;fraction<=245;broad<=1;equalize<=0;
        end
        else begin
            x<=x+1'b1;half_x<=half_x+1'b1;
            if(half_x==half_last)begin
                half_x<=0;
                if(half_line[0])x<=0;
                if(half_line==523)half_line<=0;
                else half_line<=half_line+1'b1;
                case(half_line)
                    5:begin broad<=0;equalize<=1;end
                    11:equalize<=0;
                    517:equalize<=1;
                    523:begin broad<=1;equalize<=0;end
                    default:begin end
                endcase
                if(next_fraction>=393)begin half_last<=2035;fraction<=next_fraction-10'd393;end
                else begin half_last<=2034;fraction<=next_fraction[8:0];end
            end
        end
    end
endmodule
