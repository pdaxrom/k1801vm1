`timescale 1ns/1ps
module bk_panel #(parameter integer CLOCK_HZ = 24000000) (
    input wire clk, power_on, button_n,
    input wire [2:0] active_mode,
    output reg [2:0] requested_mode = 0,
    output reg hard_reset = 1,
    output wire [8:0] seg_led_h, seg_led_l,
    input wire [2:0] boot_mode,
    input wire restore_mode,
    output reg full_reset = 0
);
    wire sample;
    uj11_tick #(.DIVISOR(CLOCK_HZ / 50)) timebase(clk,power_on,sample);
    reg [1:0] sync = 0;
    reg previous = 0, pressed = 1;
    reg [8:0] duration = 0;
    always @(posedge clk) begin
        full_reset <= 0;
        if(power_on) begin
            sync <= 0; previous <= 0; pressed <= 1; duration <= 0;
            hard_reset <= 1; requested_mode <= 0;
        end else begin
            sync <= {sync[0],button_n};
            if(restore_mode) requested_mode <= boot_mode;
            if(sample) begin
                previous <= sync[1];
                if(sync[1] == previous) begin
                    if(sync[1]) begin
                        if(pressed && !hard_reset && !restore_mode)
                            requested_mode <= requested_mode >= 6 ? 0 : requested_mode + 1'b1;
                        pressed <= 0; hard_reset <= 0; duration <= 0;
                    end else begin
                        pressed <= 1;
                        if(duration < 500) duration <= duration + 1'b1;
                        if(duration == 99) begin hard_reset <= 1; requested_mode <= boot_mode; end
                        if(duration == 499) full_reset <= 1;
                    end
                end
            end
        end
    end
    bk_frequency_display #(.CLOCK_HZ(CLOCK_HZ)) display (
        .clk(clk), .power_on(power_on), .mode(active_mode),
        .seg_led_h(seg_led_h), .seg_led_l(seg_led_l)
    );
endmodule
