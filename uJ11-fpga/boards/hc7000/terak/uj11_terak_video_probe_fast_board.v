`timescale 1ns/1ps
// Optional PAL diagnostic with faster pixel serialization; no CPU or storage.
module uj11_terak_video_probe_fast_board(
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
    uj11_terak_video_probe_board #(.PAL50(1),.FASTER(1)) board(
        .clk_ext(clk_ext),.res(res),.rx(rx),.ps2_clock(ps2_clock),.ps2_data(ps2_data),.tx(tx),
        .sram_address(sram_address),.sram_data(sram_data),.sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n),.sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .hg_tms(hg_tms),.hg_tck(hg_tck),.hg_tdi(hg_tdi),.hg_tdo(hg_tdo),
        .flash_cs_n(flash_cs_n),.flash_wp_n(flash_wp_n),.flash_hold_n(flash_hold_n),
        .flash_sck(flash_sck),.flash_mosi(flash_mosi),.led_rgb(led_rgb),
        .seg_led_h(seg_led_h),.seg_led_l(seg_led_l),.tvout(tvout),.audio(audio)
    );
endmodule
