`timescale 1ns/1ps
// HC7000 J-11 profile: 2 MiB SRAM and the canonical 22-bit I/O page.
// Decode the complete physical address before selecting any SRAM word.
module uj11_mmu_board_bus #(
    parameter integer CLOCK_HZ=24000000, TICK_DIVISOR=480000,
    parameter integer SD_SLOW_DIV=60, SD_FAST_DIV=2,
    parameter integer CLEAR_WORDS=1048576,
    parameter BOOT_ROM_ENABLE=1,VIDEO_ENABLE=0,TERMINAL_ENABLE=0,KEYBOARD_ENABLE=0,
    parameter SRAM_FAST=(CLOCK_HZ==50000000)
)(
    input wire clk,reset,power_on,peripheral_reset,dma_map_enabled,
    input wire video_clk,video_reset,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [5:0] tvout,
    input wire request,writing,byte_access,cpu_lock,
    input wire [21:0] address,
    input wire [15:0] write_data,
    output wire ready,error,
    output wire [15:0] read_data,
    output wire irq_valid,
    output wire [2:0] irq_priority,
    output wire [15:0] irq_vector,
    input wire irq_ack,
    input wire uart_rx,
    output wire uart_tx,
    input wire [3:0] panel_keys,
    output wire [7:0] panel_pins,
    output wire memory_initialized,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n,
    output wire sd_cs_n,sd_sck,sd_mosi,
    input wire sd_miso,
    output reg boot_complete,
    output wire cpu_start
);
    wire rst=reset || peripheral_reset;
    wire [1:0] lanes=byte_access ? (address[0] ? 2'b10 : 2'b01) : 2'b11;
    wire [12:0] offset={address[12:1],1'b0};
    wire io_page=&address[21:13];
    reg overlay,release_armed;
    wire rom_selected=BOOT_ROM_ENABLE && !storage_enabled && overlay && !writing && address[21:9]==13'd4;
    wire reserved=TERMINAL_ENABLE && address>=22'h1e4000 && address<22'h200000;
    wire ram_selected=!address[21] && !rom_selected && !reserved;
    wire uart_selected=io_page && offset[12:3]==(13'o17560>>3);
    wire timer_selected=io_page && offset==13'o17546;
    wire panel_selected=io_page && offset==13'o06000;
    wire maint_selected=!storage_enabled && io_page && offset==13'o17750;
    wire storage_enabled;
    wire sd_selected=io_page && offset[12:2]==(13'o17500>>2);
    wire rk_selected=!storage_enabled && io_page && offset[12:5]==(13'o17440>>5);
    // All other peripheral addresses, including absent-device probes, go to
    // SERV. Controller identity, register semantics and NXM are firmware policy.
    wire iop_selected=storage_enabled && io_page && !(uart_selected || timer_selected ||
        panel_selected || maint_selected || sd_selected);
    wire selected=ram_selected || rom_selected || uart_selected || timer_selected ||
        panel_selected || maint_selected || sd_selected || rk_selected || iop_selected;
    wire dma_request,dma_write,dma_ready,ram_ready,dma_reserved;
    wire [1:0] dma_lanes;
    wire dma_nxm=TERMINAL_ENABLE && !dma_reserved && dma_address>=22'h1e4000;
    wire mirror_ready,mirror_push;
    wire local_valid,local_ready;
    wire [7:0] local_data;
    wire [21:0] dma_address;
    wire [15:0] dma_data,ram_data;
    wire memory_request,memory_write,memory_ready;
    wire [19:0] memory_address;
    wire [1:0] memory_lanes;
    wire [15:0] memory_data;
    // SERV translates UNIBUS addresses before submitting a physical DMA burst.
    // The sector engine rejects accesses outside the installed 2 MiB SRAM.
    wire video_request,video_ready,video_reg_write;
    wire [19:0] video_address;
    wire [2:0] video_reg_address;
    wire [1:0] video_reg_lanes;
    wire [15:0] video_reg_data,video_reg_read;
    generate if(VIDEO_ENABLE)begin: pal
    uj11_video_arbiter #(.FAST_TURNAROUND(SRAM_FAST)) arbiter(.video_request(video_request),.video_address(video_address),.video_ready(video_ready),
        .clk(clk),.reset(rst),
        .cpu_request(request && ram_selected),.cpu_write(writing),.cpu_lock(cpu_lock),.cpu_address(address[20:1]),
        .cpu_lanes(lanes),.cpu_data(write_data),.cpu_ready(ram_ready),
        .dma_request(dma_request && !dma_nxm),.dma_write(dma_write),.dma_address(dma_address),
        .dma_data(dma_data),.dma_lanes(dma_lanes),.dma_ready(dma_ready),.request(memory_request),.write(memory_write),
        .address(memory_address),.lanes(memory_lanes),.data(memory_data),.ready(memory_ready));
    // Guest RESET resets its serial controller, not the external terminal.
    uj11_pal_video #(.HEIGHT(TERMINAL_ENABLE ? 240 : 200)) video(.clk(clk),.video_clk(video_clk),
        .reset(reset || video_reset || (!TERMINAL_ENABLE && peripheral_reset)),
        .reg_write(video_reg_write),.reg_address(video_reg_address),.reg_data(video_reg_data),
        .reg_lanes(video_reg_lanes),.reg_read(video_reg_read),.dma_request(video_request),
        .dma_address(video_address),.dma_ready(video_ready),.dma_data(ram_data),.dac(tvout));
    end else begin: no_pal
    uj11_mmu_sram_arbiter arbiter(.clk(clk),.reset(rst),
        .cpu_request(request && ram_selected),.cpu_write(writing),.cpu_lock(cpu_lock),.cpu_address(address[20:1]),
        .cpu_lanes(lanes),.cpu_data(write_data),.cpu_ready(ram_ready),
        .dma_request(dma_request && !dma_nxm),.dma_write(dma_write),.dma_address(dma_address),
        .dma_data(dma_data),.dma_ready(dma_ready),.request(memory_request),.write(memory_write),
        .address(memory_address),.lanes(memory_lanes),.data(memory_data),.ready(memory_ready));
        assign tvout=0,video_reg_read=0,video_request=0,video_address=0,video_ready=0;
    end endgenerate
    uj11_sram #(.CLEAR_WORDS(CLEAR_WORDS),.FAST_RESPONSE(SRAM_FAST)) memory(.clk(clk),.power_on(power_on),.reset(rst),
        .initialized(memory_initialized),.request(memory_request),.write(memory_write),
        .address(memory_address),.byte_enable(memory_lanes),.write_data(memory_data),
        .read_data(ram_data),.ready(memory_ready),.sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),
        .sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n));
    wire rk_ready,rk_irq,io_ready,io_error,io_irq,sd_ready,sd_error;
    wire [15:0] rk_data,io_data,sd_data;
    wire [8:0] io_vector;
    uj11_mmu_disk #(.CLOCK_HZ(CLOCK_HZ),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV),.VIDEO_ENABLE(VIDEO_ENABLE),.TERMINAL_ENABLE(TERMINAL_ENABLE),.KEYBOARD_ENABLE(KEYBOARD_ENABLE)) disk(
        .ps2_clock(ps2_clock),.ps2_data(ps2_data),
        .video_reg_write(video_reg_write),.video_reg_address(video_reg_address),.video_reg_data(video_reg_data),
        .video_reg_lanes(video_reg_lanes),.video_reg_read(video_reg_read),
        .clk(clk),.reset(reset),.bus_reset(peripheral_reset),.dma_map_enabled(dma_map_enabled),
        .rk_request(request && rk_selected),.rk_write(writing),
        .rk_address(address[4:1]),.rk_lanes(lanes),.rk_wdata(write_data),.rk_rdata(rk_data),
        .rk_ready(rk_ready),.rk_irq(rk_irq),.storage_enabled(storage_enabled),.cpu_start(cpu_start),.rk_irq_ack(irq_ack && irq_priority==5 && !storage_enabled),
        .io_request(request && iop_selected),.io_address(offset),.io_rdata(io_data),.io_ready(io_ready),.io_error(io_error),
        .io_irq(io_irq),.io_vector(io_vector),.io_irq_ack(irq_ack && irq_priority==5 && storage_enabled),
        .sd_request(request && sd_selected),.sd_write(writing),.sd_byte(byte_access),
        .sd_address(address[1:0]),.sd_wdata(write_data),.sd_rdata(sd_data),.sd_ready(sd_ready),.sd_error(sd_error),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .dma_request(dma_request),.dma_write(dma_write),.dma_address(dma_address),.dma_data(dma_data),.dma_unibus(),
        .dma_lanes(dma_lanes),.dma_reserved(dma_reserved),.mirror_push(mirror_push),.mirror_data(write_data[7:0]),.mirror_ready(mirror_ready),
        .local_valid(local_valid),.local_data(local_data),.local_ready(local_ready),
        .dma_ready(dma_ready || dma_nxm),.dma_error(dma_nxm),.dma_rdata(ram_data));
    wire rx_irq,tx_irq,uart_ready;
    wire [15:0] uart_data;
    wire uart_ack=irq_ack && irq_priority==4;
    wire console_write=writing && !address[0] && address[2:1]==3;
    wire console_request=request && uart_selected && (!console_write || mirror_ready);
    assign mirror_push=console_request && console_write && !uart_ready;
    uj11_console_uart #(.REFCLK(CLOCK_HZ),.LOCAL_ENABLE(KEYBOARD_ENABLE)) console(.wb_clk_i(clk),.wb_rst_i(rst),
        .wb_adr_i(address[2:0]),.wb_dat_i(write_data),.wb_dat_o(uart_data),
        .wb_cyc_i(console_request),.wb_stb_i(console_request),
        .wb_we_i(writing),.wb_ack_o(uart_ready),.tx_dat_o(uart_tx),.tx_cts_i(1'b0),
        .local_valid(local_valid),.local_data(local_data),.local_ready(local_ready),
        .rx_dat_i(uart_rx),.rx_dtr_o(),.tx_irq_o(tx_irq),.rx_irq_o(rx_irq),
        .tx_ack_i(uart_ack && !rx_irq),.rx_ack_i(uart_ack && rx_irq));
    wire [3:0] hg_rows;
    wire [15:0] panel_data;
    uj11_hg_inputs hg(.clk(clk),.reset(rst),.output_enable(panel_pins[7]),
        .rows(panel_keys),.filtered(hg_rows));
    uj11_panel panel(.clk(clk),.reset(rst),
        .write_enable(request && panel_selected && writing && lanes[1]),
        .rows(hg_rows),.write_data(write_data[15:8]),.pins(panel_pins),.read_data(panel_data));
    wire tick;
    reg timer_done,timer_ie,timer_pending;
    uj11_tick #(.DIVISOR(TICK_DIVISOR)) timer(.clk(clk),.reset(rst),.terminal(tick));
    always @(posedge clk) begin
        if(rst)begin timer_done<=1;timer_ie<=0;timer_pending<=0;end
        else begin
            if(irq_ack && irq_priority==6)timer_pending<=0;
            if(tick)begin timer_done<=1;if(timer_ie)timer_pending<=1;end
            if(request && timer_selected && writing && lanes[0])begin
                timer_ie<=write_data[6];
                if(!write_data[7])timer_done<=0;
                if(!write_data[6])timer_pending<=0;
                else if(write_data[7] && timer_done)timer_pending<=1;
            end
        end
    end
    wire disk_irq=storage_enabled ? io_irq : rk_irq;
    assign irq_valid=timer_pending || disk_irq || rx_irq || tx_irq;
    assign irq_priority=timer_pending ? 3'd6 : disk_irq ? 3'd5 : 3'd4;
    assign irq_vector=timer_pending ? 16'o100 : disk_irq ? (storage_enabled ? {7'b0,io_vector} : 16'o210) : rx_irq ? 16'o60 : 16'o64;
    wire [15:0] rom_data;
    reg [1:0] rom_phase;
    uj11_mmu_boot_rom boot(.clk(clk),.enable(rom_phase==1),.address({1'b0,address[8:1]}),.data(rom_data));
    always @(posedge clk) begin
        if(rst || !request)rom_phase<=0;
        else if(rom_selected && rom_phase!=3)rom_phase<=rom_phase+1'b1;
        if(reset)begin overlay<=1;release_armed<=0;boot_complete<=!BOOT_ROM_ENABLE;end
        else begin
            if(request && sd_selected && sd_ready && writing && address[1] && write_data[2])release_armed<=1;
            if(release_armed && request && !writing && address==0)begin
                overlay<=0;release_armed<=0;boot_complete<=1;
            end
        end
    end
    // Only the legacy MMU profile retains this fixed MAINT value. The storage
    // profile obtains board identification and UNIBUS map CSRs from SERV.
    assign read_data=({16{ram_selected}} & ram_data) | ({16{rom_selected}} & rom_data) |
        ({16{uart_selected}} & uart_data) | ({16{sd_selected}} & sd_data) |
        ({16{iop_selected}} & io_data) | ({16{rk_selected}} & rk_data) | ({16{panel_selected}} & panel_data) |
        ({16{timer_selected}} & {8'b0,timer_done,timer_ie,6'b0}) | ({16{maint_selected}} & 16'o31);
    assign ready=request && ((ram_selected && ram_ready) || (rom_selected && rom_phase==3) ||
        (uart_selected && uart_ready) || (sd_selected && sd_ready) || (rk_selected && rk_ready) || (iop_selected && io_ready) ||
        timer_selected || panel_selected || maint_selected || !selected);
    assign error=request && (!selected || (sd_selected && sd_ready && sd_error) || (iop_selected && io_ready && io_error));
endmodule
