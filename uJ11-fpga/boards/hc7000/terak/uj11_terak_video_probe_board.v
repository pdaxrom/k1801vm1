`timescale 1ns/1ps
// Standalone video measurement image. SD, SRAM and external Flash are idle.
module uj11_terak_video_probe_board #(parameter PAL50=0,FASTER=0,MEASURE=0,PROGRESSIVE=0)(
    input wire clk_ext,res,rx,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire tx,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n,
    output wire sd_cs_n,sd_sck,sd_mosi,input wire sd_miso,
    input wire hg_tms,hg_tck,hg_tdi,inout wire hg_tdo,
    output wire flash_cs_n,flash_wp_n,flash_hold_n,flash_sck,flash_mosi,
    output wire [2:0] led_rgb,
    output wire [8:0] seg_led_h,seg_led_l,
    output wire [5:0] tvout,output wire [1:0] audio
);
    wire video_clk,locked;
    uj11_pal_pll pll(.CLKI(clk_ext),.CLKOP(),.CLKOS(video_clk),.LOCK(locked));
    reg [2:0] reset_pipe=7;
    always @(posedge video_clk or negedge locked or negedge res)
        if(!locked || !res)reset_pipe<=7;
        else reset_pipe<={reset_pipe[1:0],1'b0};
    uj11_terak_video_probe #(.PAL50(PAL50),.FASTER(FASTER),.MEASURE(MEASURE),.PROGRESSIVE(PROGRESSIVE)) probe(
        .clk(video_clk),.reset(reset_pipe[2]),.dac(tvout));
    assign sram_address=0,sram_data=16'bz;
    assign sram_ce_n=1,sram_oe_n=1,sram_we_n=1,sram_lb_n=1,sram_ub_n=1;
    assign sd_cs_n=1,sd_sck=0,sd_mosi=0;
    assign flash_cs_n=1,flash_wp_n=1,flash_hold_n=1,flash_sck=0,flash_mosi=0;
    assign ps2_clock=1'bz,hg_tdo=1'bz,tx=1;
    assign led_rgb=3'b111,seg_led_h=9'h1ff,seg_led_l=9'h1ff,audio=0;
endmodule
