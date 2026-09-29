`timescale 1ns/1ps
module uj11_mmu_microcomp #(
    parameter integer CLOCK_HZ=24000000,
    parameter integer DIAGNOSTICS_ENABLE=0
)(
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
    generate if(CLOCK_HZ==50000000)begin: clock50
        uj11_hc7000_pll50 pll(.CLKI(clk_ext),.CLKOP(),.CLKOS(clk),.LOCK(locked));
    end else begin: clock24
        uj11_hc7000_pll pll(.CLKI(clk_ext),.CLKOP(clk),.CLKOS(),.LOCK(locked));
    end endgenerate
    reg [3:0] power_on=4'b1111;
    always @(posedge clk or negedge locked)
        if(!locked) power_on<=4'b1111;
        else power_on<={power_on[2:0],1'b0};
    wire hard_reset, halt_button, memory_initialized;
    uj11_button #(.SAMPLE_DIVISOR(CLOCK_HZ/50)) button(
        .clk(clk),.power_on(power_on[3]),.button_n(res),
        .hard_reset(hard_reset),.halt_pulse(halt_button));
    reg system_reset=1'b1;
    always @(posedge clk)system_reset<=power_on[3] || hard_reset || !memory_initialized;
    wire [7:0] panel_pins;
    wire stopped,diagnostic_halt,diagnostic_wait,diagnostic_retire;
    wire [1:0] diagnostic_mode;
    wire host_miso=panel_pins[6], host_miso_oe=panel_pins[7];
    assign hg_tdo=host_miso_oe ? host_miso : 1'bz;
    uj11_mmu_board #(.CLOCK_HZ(CLOCK_HZ),.TICK_DIVISOR(CLOCK_HZ/50),
        .SD_SLOW_DIV((CLOCK_HZ+399999)/400000),.SD_FAST_DIV(2)) system(
        .clk(clk),.reset(CLOCK_HZ==50000000 ? system_reset : power_on[3] || hard_reset || !memory_initialized),
        .power_on(power_on[3]),.memory_initialized(memory_initialized),
        .halt_button(halt_button),.uart_rx(rx),.uart_tx(tx),
        .panel_keys({1'b0,hg_tdi,hg_tck,hg_tms}),
        .panel_pins(panel_pins),
        .sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),
        .sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .boot_complete(),.stopped(stopped),.diagnostic_halt(diagnostic_halt),
        .diagnostic_wait(diagnostic_wait),.diagnostic_retire(diagnostic_retire),.diagnostic_mode(diagnostic_mode));
    uj11_hc7000_diagnostics #(.CLOCK_HZ(CLOCK_HZ),.ENABLE(DIAGNOSTICS_ENABLE)) diagnostics(
        .clk(clk),.power_on(power_on[3]),.reset_active(system_reset || hard_reset),
        .console_active(stopped),.console_halt(diagnostic_halt),.waiting(diagnostic_wait),
        .mode(diagnostic_mode),.retire(diagnostic_retire),.disk_active(!sd_cs_n),
        .seg_led_h(seg_led_h),.seg_led_l(seg_led_l));
    assign flash_cs_n=1'b1, flash_wp_n=1'b1, flash_hold_n=1'b1;
    assign flash_sck=1'b0, flash_mosi=1'b0;
    assign led_rgb=3'b111;
    assign tvout=6'b0, audio=2'b0;
endmodule
