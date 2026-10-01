`timescale 1ns/1ps
module uj11_mmu_board #(
    parameter integer CLOCK_HZ=24000000,TICK_DIVISOR=480000,
    parameter integer SD_SLOW_DIV=60,SD_FAST_DIV=2,CLEAR_WORDS=1048576,VIDEO_ENABLE=0,TERMINAL_ENABLE=0
)(
    input wire clk,reset,power_on,uart_rx,halt_button,
    input wire video_clk,video_reset,
    output wire [5:0] tvout,
    output wire memory_initialized,uart_tx,
    input wire [3:0] panel_keys,
    output wire [7:0] panel_pins,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n,
    output wire sd_cs_n,sd_sck,sd_mosi,
    input wire sd_miso,
    output wire boot_complete,stopped,
    output wire diagnostic_halt,diagnostic_wait,diagnostic_retire,
    output wire [1:0] diagnostic_mode
);
    wire request,writing,byte_access,ready,error,peripheral_reset,irq_valid,irq_ack,cpu_lock;
    wire [21:0] address;
    wire [15:0] write_data,read_data,irq_vector,mmr3;
    wire [2:0] irq_priority;
    wire [15:0] cpu_psw;
    wire cpu_start;
    assign diagnostic_mode=cpu_psw[15:14];
    uj11_mmu_cpu cpu(.clk(clk),.reset(reset || !cpu_start),.halt_button(halt_button),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .peripheral_reset(peripheral_reset),.mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_lock(cpu_lock),
        .mem_address(address),.mem_write_data(write_data),.mem_ready(ready),.mem_error(error),
        .mem_read_data(read_data),.console_active(stopped),.console_halt(diagnostic_halt),
        .wait_active(diagnostic_wait),.waiting(),.retire(diagnostic_retire),.psw(cpu_psw),.ir(),
        .mmr0(),.mmr1(),.mmr2(),.mmr3(mmr3),.upc(),.uword(),.pc(),.debug_register_data(),.debug_register_address(5'd0));
    uj11_mmu_board_bus #(.CLOCK_HZ(CLOCK_HZ),.TICK_DIVISOR(TICK_DIVISOR),
        .SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV),.CLEAR_WORDS(CLEAR_WORDS),.VIDEO_ENABLE(VIDEO_ENABLE),.TERMINAL_ENABLE(TERMINAL_ENABLE)) bus(
        .video_clk(video_clk),.video_reset(video_reset),.tvout(tvout),
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),.dma_map_enabled(mmr3[5]),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(cpu_lock),.address(address),.write_data(write_data),
        .ready(ready),.error(error),.read_data(read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .panel_keys(panel_keys),.panel_pins(panel_pins),.memory_initialized(memory_initialized),
        .sram_address(sram_address),.sram_data(sram_data),.sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n),.sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),.boot_complete(boot_complete),.cpu_start(cpu_start));
endmodule
