`timescale 1ns/1ps
module bk_panel #(parameter integer CLOCK_HZ = 24000000) (
    input wire clk, power_on, button_n,
    input wire [1:0] active_mode,
    output reg [1:0] requested_mode = 0,
    output wire hard_reset,
    output wire [8:0] seg_led_h, seg_led_l
);
    wire short_press;
    uj11_button #(.SAMPLE_DIVISOR(CLOCK_HZ / 50), .HOLD_SAMPLES(100)) button (
        .clk(clk), .power_on(power_on), .button_n(button_n),
        .hard_reset(hard_reset), .halt_pulse(short_press)
    );
    always @(posedge clk) begin
        if (power_on) requested_mode <= 0;
        else if (short_press) requested_mode <= requested_mode + 1'b1;
    end
    bk_frequency_display #(.CLOCK_HZ(CLOCK_HZ)) display (
        .clk(clk), .power_on(power_on), .mode(active_mode),
        .seg_led_h(seg_led_h), .seg_led_l(seg_led_l)
    );
endmodule
