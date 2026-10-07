`timescale 1ns/1ps
// Monochrome PAL-60 presentation: 525 interlaced lines, exactly 60 fields/s
// at nominal 64 MHz. 63 half-lines consume 128000 clocks; rounding is carried
// across fields/frames, giving 2031/2032-clock half-lines without clock drift.
// Half-line zero is the first broad vertical pulse, following pre-equalizing.
module uj11_terak_timing(
    input wire clk,reset,
    output reg [11:0] x,
    output reg [10:0] half_line,
    output wire [9:0] line_number,
    output wire line_start,frame_start,field_start,field,
    output wire sync,active_line,active,prefetch,
    output wire [7:0] row,next_row
);
    reg [10:0] half_x,half_last;
    reg [5:0] fraction;
    wire [6:0] next_fraction={1'b0,fraction}+7'd47;
    assign line_number=half_line[10:1];
    assign line_start=x==0;
    assign frame_start=half_line==0 && half_x==0;
    assign field_start=(half_line==0 || half_line==525) && half_x==0;
    assign field=half_line>=525;
    // Pulse mode changes only at half-line boundaries. Registering the mode
    // removes the vertical range-decode chain from the 64 MHz DAC path.
    reg broad,equalize;
    // 4.703 us line sync, 2.297 us equalizing, and one line-sync-width gap
    // between broad pulses. Colour subcarrier/burst is unnecessary for Terak.
    assign sync=broad ? half_x<half_last-11'd300 : equalize ? half_x<147 : x<301;
    assign active_line=(line_number>=18 && line_number<258) ||
        (line_number>=281 && line_number<521);
    assign row=field ? line_number-281 : line_number-18;
    // Position/width proven by the display probe: all 640 columns occupy
    // 3180 clocks, with twenty four-clock pixels and 620 five-clock pixels.
    assign active=active_line && x>=695 && x<3875;
    assign prefetch=(line_number>=17 && line_number<257) ||
        (line_number>=280 && line_number<520);
    assign next_row=field ? line_number-280 : line_number-17;
    always @(posedge clk)begin
        if(reset)begin
            x<=0;half_x<=0;half_line<=0;half_last<=2030;fraction<=47;broad<=1;equalize<=0;
        end
        else begin
            x<=x+1'b1;half_x<=half_x+1'b1;
            if(half_x==half_last)begin
                half_x<=0;
                if(half_line[0])x<=0;
                if(half_line==1049)half_line<=0;
                else half_line<=half_line+1'b1;
                case(half_line)
                    5,530:begin broad<=0;equalize<=1;end
                    11,536:equalize<=0;
                    518,1043:equalize<=1;
                    524,1049:begin broad<=1;equalize<=0;end
                    default:begin end
                endcase
                if(next_fraction>=63)begin half_last<=2031;fraction<=next_fraction-7'd63;end
                else begin half_last<=2030;fraction<=next_fraction[5:0];end
            end
        end
    end
endmodule
