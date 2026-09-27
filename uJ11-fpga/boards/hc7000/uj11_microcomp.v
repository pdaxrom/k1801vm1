`timescale 1ns/1ps
module uj11_hc7000_microcomp(
    input wire clk_ext, res, rx,
    output wire tx,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n, sram_oe_n, sram_we_n, sram_lb_n, sram_ub_n,
    output wire sd_cs_n, sd_sck, sd_mosi, input wire sd_miso,
    input wire hg_tms, hg_tck, hg_tdi, inout wire hg_tdo,
    output wire flash_cs_n, flash_wp_n, flash_hold_n, flash_sck, flash_mosi,
    output wire [2:0] led_rgb,
    output wire [8:0] seg_led_h, seg_led_l,
    output wire [5:0] tvout, output wire [1:0] audio
);
    wire clk, locked;
    uj11_hc7000_pll pll(.CLKI(clk_ext),.CLKOP(clk),.CLKOS(),.LOCK(locked));
    reg [3:0] power_on=4'b1111;
    always @(posedge clk or negedge locked)
        if(!locked) power_on<=4'b1111;
        else power_on<={power_on[2:0],1'b0};
    wire hard_reset, halt_button, memory_initialized;
    uj11_button #(.SAMPLE_DIVISOR(480000)) button(
        .clk(clk),.power_on(power_on[3]),.button_n(res),
        .hard_reset(hard_reset),.halt_pulse(halt_button));
    wire host_miso, host_miso_oe;
    assign hg_tdo=host_miso_oe ? host_miso : 1'bz;
    uj11_hc7000_board system(
        .clk(clk),.reset(power_on[3] || hard_reset || !memory_initialized),
        .power_on(power_on[3]),.memory_initialized(memory_initialized),
        .halt_button(halt_button),.uart_rx(rx),.uart_tx(tx),
        .panel_keys({1'b0,hg_tdi,hg_tck,hg_tms}),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(host_miso),.host_miso_oe(host_miso_oe),
        .sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),
        .sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .boot_complete(),.stopped());
    assign flash_cs_n=1'b1, flash_wp_n=1'b1, flash_hold_n=1'b1;
    assign flash_sck=1'b0, flash_mosi=1'b0;
    assign led_rgb=3'b111;
    assign seg_led_h=9'h0ff, seg_led_l=9'h0ff;
    assign tvout=6'b0, audio=2'b0;
endmodule
