`timescale 1ns/1ps
// 64 MHz, ITU-R BT.1700 625/50. The half-line counter starts at the
// first broad vertical pulse (line 1), not at the pre-equalizing sequence.
// One complete frame is 1250 half-lines; the second field starts halfway
// through line 313. Picture rows are repeated in the two interlaced fields.
module uj11_pal_timing #(parameter integer HEIGHT=200)(
    input wire clk, reset,
    output reg [11:0] x,
    output reg [10:0] half_line,
    output wire [9:0] line_number,
    output wire line_start, frame_start, field,
    output wire sync, burst, active_line, active,
    output wire [7:0] row,
    output reg v_alternate
);
    reg colour_frame;
    assign line_number=half_line[10:1];
    assign line_start=x==0;
    assign frame_start=line_start && half_line==0;
    assign field=half_line>=625;
    localparam integer FIRST=56-(HEIGHT-200)/2, SECOND=369-(HEIGHT-200)/2;
    wire broad=half_line<5 || (half_line>=625 && half_line<630);
    wire equalize=(half_line>=5 && half_line<10) ||
        (half_line>=620 && half_line<625) ||
        (half_line>=630 && half_line<635) || half_line>=1245;
    assign sync=broad ? x[10:0]<1747 : equalize ? x[10:0]<150 : x<301;
    // Four-field burst-blanking sequence; the continuous DDS extends the
    // colour phase sequence to eight fields. line_number is zero based.
    wire burst_blank=colour_frame ?
        (line_number<5 || line_number>=621 || (line_number>=310 && line_number<=318)) :
        (line_number<6 || line_number>=622 || (line_number>=309 && line_number<=317));
    assign burst=!burst_blank && x>=358 && x<502;
    assign active_line=(line_number>=FIRST && line_number<FIRST+HEIGHT) ||
        (line_number>=SECOND && line_number<SECOND+HEIGHT);
    assign row=field ? line_number-SECOND : line_number-FIRST;
    assign active=active_line && x>=768 && x<3968;
    always @(posedge clk) begin
        if(reset) begin x<=0;half_line<=0;colour_frame<=0;v_alternate<=0;end
        else begin
            x<=x+1'b1;
            if(x==4095)v_alternate<=!v_alternate;
            if(x[10:0]==2047) begin
                if(half_line==1249) begin half_line<=0;colour_frame<=!colour_frame;end
                else half_line<=half_line+1'b1;
            end
        end
    end
endmodule
