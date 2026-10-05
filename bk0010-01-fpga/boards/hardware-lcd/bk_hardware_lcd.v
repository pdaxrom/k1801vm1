`timescale 1ns/1ps
module bk_hardware_lcd (
    input wire clk_ext, res,
    output wire tx,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n, sram_oe_n, sram_we_n, sram_lb_n, sram_ub_n,
    output wire sd_cs_n, sd_sck, sd_mosi,
    input wire sd_miso,
    output wire flash_cs_n, flash_wp_n, flash_hold_n, flash_sck, flash_mosi,
    output wire [2:0] led_rgb,
    output wire [8:0] seg_led_h, seg_led_l,
    output wire [5:0] tvout,
    output wire [1:0] audio
);
    // Preserve names used by the physical LPF across generated-clock mapping.
    wire clk /* synthesis syn_keep=1 */;
    wire clk48 /* synthesis syn_keep=1 */;
    wire video_clk /* synthesis syn_keep=1 */;
    wire main_lock, video_lock;
    bk_main_pll main_pll(.CLKI(clk_ext), .CLKOP(clk), .CLKOS(clk48), .LOCK(main_lock));
    bk_video_pll video_pll(.CLKI(clk_ext), .CLKOP(), .CLKOS(video_clk), .LOCK(video_lock));
    wire locked = main_lock && video_lock;
    reg [3:0] power_on = 15;
    always @(posedge clk or negedge locked)
        if(!locked) power_on <= 15;
        else power_on <= {power_on[2:0],1'b0};
    wire initialized, rom_locked, boot_error, underrun;
    wire [7:0] stage;
    bk_computer machine (
        .clk(clk), .clk48(clk48), .video_clk(video_clk), .power_on(power_on[3]), .button_n(res),
        .ps2_clock(ps2_clock), .ps2_data(ps2_data),
        .sram_address(sram_address), .sram_data(sram_data), .sram_ce_n(sram_ce_n), .sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n), .sram_lb_n(sram_lb_n), .sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n), .sd_sck(sd_sck), .sd_mosi(sd_mosi), .sd_miso(sd_miso),
        .tvout(tvout), .audio(audio), .uart_tx(tx), .seg_led_h(seg_led_h), .seg_led_l(seg_led_l),
        .memory_initialized(initialized), .rom_locked(rom_locked), .boot_error(boot_error),
        .video_underrun(underrun), .boot_stage(stage)
    );
    assign flash_cs_n = 1, flash_wp_n = 1, flash_hold_n = 1;
    assign flash_sck = 0, flash_mosi = 0;
    assign led_rgb = {!(boot_error || underrun), !rom_locked, !initialized};
endmodule
