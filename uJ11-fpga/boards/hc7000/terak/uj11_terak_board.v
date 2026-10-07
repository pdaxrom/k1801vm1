`timescale 1ns/1ps
module uj11_terak_board #(
    parameter integer CLOCK_HZ=50000000,TICK_DIVISOR=833333,SD_SLOW_DIV=125,SD_FAST_DIV=2,CLEAR_WORDS=1048576,
    parameter VIDEO_ENABLE=1,TERMINAL_ENABLE=0,KEYBOARD_ENABLE=1,DISPLAY_EBR=0,RAM_BYTES=57344,
    parameter MICROCODE="build/hc7000-terak/cpu/microcode.mem"
)(
    input wire clk,reset,power_on,uart_rx,halt_button,video_clk,video_reset,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [5:0] tvout,
    output wire [1:0] audio,
    output wire memory_initialized,uart_tx,
    input wire [3:0] panel_keys,
    output wire [7:0] panel_pins,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n,
    output wire sd_cs_n,sd_sck,sd_mosi,
    input wire sd_miso,
    output wire boot_complete,stopped,diagnostic_halt,diagnostic_wait,diagnostic_retire,
    output wire [1:0] diagnostic_mode
);
    wire request,writing,byte_access,ready,error,peripheral_reset,irq_valid,irq_ack,cpu_lock,cpu_start;
    wire [21:0] address;
    wire [15:0] write_data,read_data,irq_vector,psw;
    wire [2:0] irq_priority;
    assign diagnostic_mode=psw[15:14];
    assign uart_tx=1'b1,panel_pins=0;
    uj11_mmu_cpu #(.BOOT_PC(16'o173000),.REGISTER_MICROCODE(1),
        .MICROCODE(MICROCODE)) cpu(.clk(clk),.reset(reset || !cpu_start),.halt_button(halt_button),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .peripheral_reset(peripheral_reset),.mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_lock(cpu_lock),
        .mem_address(address),.mem_write_data(write_data),.mem_ready(ready),.mem_error(error),.mem_read_data(read_data),
        .console_active(stopped),.console_halt(diagnostic_halt),.wait_active(diagnostic_wait),.waiting(),
        .retire(diagnostic_retire),.psw(psw),.ir(),.mmr0(),.mmr1(),.mmr2(),.mmr3(),.upc(),.uword(),.pc(),
        .debug_register_data(),.debug_register_address(5'b0));
    uj11_terak_bus #(.CLOCK_HZ(CLOCK_HZ),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV),
        .CLEAR_WORDS(CLEAR_WORDS),.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES)) bus(
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),.video_clk(video_clk),.video_reset(video_reset),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(cpu_lock),.address(address),.write_data(write_data),
        .ready(ready),.error(error),.read_data(read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .ps2_clock(ps2_clock),.ps2_data(ps2_data),.tvout(tvout),.audio(audio),.memory_initialized(memory_initialized),
        .cpu_start(cpu_start),.boot_complete(boot_complete),.sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),.sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso));
endmodule
