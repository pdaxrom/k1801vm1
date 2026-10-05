`timescale 1ns/1ps
// Adapted from uJ11 HC7000 diagnostics: one segment across both digits.
module bk_frequency_display #(parameter integer CLOCK_HZ = 24000000) (
    input wire clk, power_on,
    input wire [1:0] mode,
    output reg [8:0] seg_led_h = 9'h0ff, seg_led_l = 9'h0ff
);
    localparam integer DIVISOR = CLOCK_HZ < 32000 ? 1 : CLOCK_HZ / 32000;
    localparam integer WIDTH = DIVISOR < 2 ? 1 : $clog2(DIVISOR);
    localparam integer LIMIT = DIVISOR - 1;
    reg [WIDTH-1:0] divider = 0;
    reg [1:0] phase = 0;
    reg [3:0] slot = 0;
    reg [15:0] frame = 0;
    wire tick = divider == LIMIT[WIDTH-1:0];
    function [6:0] digit;
        input [3:0] number;
        begin
            case (number)
                0: digit = 7'h40;
                1: digit = 7'h79;
                2: digit = 7'h24;
                3: digit = 7'h30;
                4: digit = 7'h19;
                5: digit = 7'h12;
                6: digit = 7'h02;
                7: digit = 7'h78;
                8: digit = 7'h00;
                9: digit = 7'h10;
                default: digit = 7'h7f;
            endcase
        end
    endfunction
    wire [3:0] tens = mode == 3 ? 4'd2 : mode == 2 ? 4'd1 : 4'd0;
    wire [3:0] units = mode == 3 ? 4'd4 : mode == 2 ? 4'd2 : mode == 1 ? 4'd6 : 4'd3;
    always @(posedge clk) begin
        if (power_on) begin
            divider <= 0; phase <= 0; slot <= 0; frame <= 0;
            seg_led_h <= 9'h0ff; seg_led_l <= 9'h0ff;
        end else begin
            divider <= tick ? 0 : divider + 1'b1;
            if (tick) begin
                phase <= phase + 1'b1;
                case (phase)
                    0: begin
                        seg_led_h[8] <= 0; seg_led_l[8] <= 0;
                        if (slot == 0) frame <= {1'b0, ~digit(units), 1'b0, ~digit(tens)};
                    end
                    1: begin
                        seg_led_h[7:0] <= !slot[3] && frame[slot] ? ~(8'b1 << slot[2:0]) : 8'hff;
                        seg_led_l[7:0] <= slot[3] && frame[slot] ? ~(8'b1 << slot[2:0]) : 8'hff;
                    end
                    2: begin
                        seg_led_h[8] <= !slot[3] && frame[slot];
                        seg_led_l[8] <= slot[3] && frame[slot];
                    end
                    3: begin
                        seg_led_h[8] <= 0; seg_led_l[8] <= 0;
                        slot <= slot + 1'b1;
                    end
                endcase
            end
        end
    end
endmodule
