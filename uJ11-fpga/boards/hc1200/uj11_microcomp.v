`timescale 1ns/1ps
module uj11_hc1200_microcomp(
    output wire sd_cs_n, sd_sck, sd_mosi, input wire sd_miso,
    input wire res, rx, output wire tx,
    output wire gpio_mosi, input wire gpio_miso,
    output wire gpio_mcs /* synthesis syn_useioff = 1 */,
    output wire gpio_msck, gpio_din, gpio_ce, gpio_clk,
    output wire gpio_rs, gpio_blank, gpio_reg_latch, inout wire [3:0] gpio_key_row
);
    wire clk;
    reg [1:0] power_on=2'b11;
    always @(posedge clk) power_on<={1'b0,power_on[1]};
    wire hard_reset, halt_button;
    uj11_button button(.clk(clk),.power_on(power_on[0]),.button_n(res),
        .hard_reset(hard_reset),.halt_pulse(halt_button));
    OSCH #(.NOM_FREQ("29.56")) oscillator(.STDBY(1'b0),.OSC(clk));
    wire host_miso, host_miso_oe;
    assign gpio_key_row[3]=host_miso_oe ? host_miso : 1'bz;
    // This board measured 50.409051 Hz with /591200 (2026-09-22).
    // Calibrate KW11 only; OSCH and peripheral clocks keep their settings.
    uj11_board #(.TICK_DIVISOR(596037)) system(.clk(clk),.reset(power_on[0] || hard_reset),.halt_button(halt_button),.uart_rx(rx),.uart_tx(tx),
        .panel_keys(gpio_key_row),.panel_din(gpio_din),.panel_ce(gpio_ce),.panel_clk(gpio_clk),
        .panel_rs(gpio_rs),.panel_blank(gpio_blank),.panel_latch(gpio_reg_latch),
        .host_miso(host_miso),.host_miso_oe(host_miso_oe),
        .fram_cs_n(gpio_mcs),.fram_sck(gpio_msck),.fram_mosi(gpio_mosi),.fram_miso(gpio_miso),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .boot_complete(),.stopped());
endmodule
