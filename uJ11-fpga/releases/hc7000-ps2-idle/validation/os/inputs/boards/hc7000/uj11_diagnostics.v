`timescale 1ns/1ps
// HC7000 common-anode display: at most one segment across both digits.
// This reduces aggregate current; duty cycle/DRIVE do not limit peak current.
module uj11_hc7000_diagnostics #(
    parameter integer CLOCK_HZ=50000000,
    parameter integer ENABLE=0
)(
    input wire clk,power_on,reset_active,
    input wire console_active,console_halt,waiting,
    input wire [1:0] mode,
    input wire retire,disk_active,
    output wire [8:0] seg_led_h,seg_led_l
);
    generate if(ENABLE!=0)begin: enabled
        // Four phases per segment, sixteen segments (including both DPs).
        // 32 kHz phase clock -> ~500 Hz frames, 1/64 duty per lit segment.
        localparam integer DIVISOR=CLOCK_HZ<32000 ? 1 : CLOCK_HZ/32000;
        localparam integer WIDTH=DIVISOR<2 ? 1 : $clog2(DIVISOR);
        localparam integer LIMIT=DIVISOR-1;
        reg [WIDTH-1:0] divider=0;
        wire tick=divider==LIMIT[WIDTH-1:0];
        reg [4:0] sub_ms=0;
        wire ms_tick=tick && sub_ms==31;
        reg [1:0] phase=0;
        reg [3:0] slot=0;
        reg [15:0] frame_segments=0;
        reg [8:0] high_digit=9'h0ff,low_digit=9'h0ff;
        reg [6:0] cpu_hold=0,disk_hold=0;
        // Active-low {g,f,e,d,c,b,a}; S and 5 share a glyph.
        localparam [6:0] H=7'h09,L=7'h47,ZERO=7'h40,D=7'h21,
            U=7'h41,S=7'h12,Y=7'h11,R=7'h2f,DASH=7'h3f;
        wire [13:0] glyphs=reset_active ? {R,S} :
            console_active ? (console_halt ? {H,L} : {ZERO,D}) :
            waiting ? {DASH,DASH} : mode==0 ? {S,Y} :
            mode==1 ? {S,U} : mode==3 ? {U,S} : {DASH,DASH};
        always @(posedge clk)begin
            // Only power-on resets the scanner. A held board reset must
            // remain visible, and guest RESET must not blank the display.
            if(power_on)begin
                divider<=0;sub_ms<=0;phase<=0;slot<=0;frame_segments<=0;
                high_digit<=9'h0ff;low_digit<=9'h0ff;
                cpu_hold<=0;disk_hold<=0;
            end else begin
                divider<=tick ? 0 : divider+1'b1;
                if(tick)sub_ms<=sub_ms+1'b1;
                if(reset_active)begin cpu_hold<=0;disk_hold<=0;end
                else begin
                    if(retire && !console_active)cpu_hold<=100;
                    else if(ms_tick && cpu_hold!=0)cpu_hold<=cpu_hold-1'b1;
                    if(disk_active)disk_hold<=100;
                    else if(ms_tick && disk_hold!=0)disk_hold<=disk_hold-1'b1;
                end
                if(tick)begin
                    phase<=phase+1'b1;
                    case(phase)
                        0:begin
                            high_digit[8]<=0;low_digit[8]<=0;
                            // Coherent frame; slot 0..7 is left A..DP,
                            // slot 8..15 is right A..DP. A set bit lights.
                            if(slot==0)frame_segments<={disk_hold!=0,~glyphs[6:0],cpu_hold!=0,~glyphs[13:7]};
                        end
                        1:begin
                            // Change cathodes only after both commons have
                            // been off; unused slots remain entirely blank.
                            high_digit[7:0]<=!slot[3] && frame_segments[slot] ? ~(8'b1<<slot[2:0]) : 8'hff;
                            low_digit[7:0]<=slot[3] && frame_segments[slot] ? ~(8'b1<<slot[2:0]) : 8'hff;
                        end
                        2:begin
                            high_digit[8]<=!slot[3] && frame_segments[slot];
                            low_digit[8]<=slot[3] && frame_segments[slot];
                        end
                        3:begin
                            high_digit[8]<=0;low_digit[8]<=0;slot<=slot+1'b1;
                        end
                    endcase
                end
            end
        end
        assign seg_led_h=high_digit,seg_led_l=low_digit;
    end else begin: disabled
        assign seg_led_h=9'h0ff,seg_led_l=9'h0ff;
    end endgenerate
endmodule
