`timescale 1ns/1ps
module uj11_terak_board #(
    parameter integer CLOCK_HZ=50000000,QX_MECHANICAL_SPEEDUP=1,TICK_DIVISOR=833333,SD_SLOW_DIV=125,SD_FAST_DIV=2,CLEAR_WORDS=1048576,
    parameter VIDEO_ENABLE=1,TERMINAL_ENABLE=0,KEYBOARD_ENABLE=1,DISPLAY_EBR=0,RAM_BYTES=57344,BOOT_BIOS=0,RL_ENABLE=0,
    parameter MICROCODE="build/hc7000-terak/cpu/microcode.mem"
)(
    input wire clk,reset,cold_reset,power_on,uart_rx,halt_button,video_clk,video_reset,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [5:0] tvout,
    output wire [1:0] audio,
    output wire memory_initialized,uart_tx,storage_idle,
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
    wire bus_ready,bus_error;
    wire [15:0] bus_read_data;
    wire [2:0] irq_priority;
    assign diagnostic_mode=psw[15:14];
    assign uart_tx=1'b1,panel_pins=0;
    wire cpu_reset;
    generate if(BOOT_BIOS)begin: registered_start
        reg held;
        // SERV releases the guest only after BIOS DMA has completed. Register
        // this boundary to keep its high-fanout startup signal off bus paths.
        always @(posedge clk)held<=reset || !cpu_start;
        assign cpu_reset=held;
    end else begin: direct_start
        assign cpu_reset=reset || !cpu_start;
    end endgenerate
    generate if(BOOT_BIOS)begin: registered_response
        reg valid,failed;
        reg [15:0] data;
        // The bus master holds the request through completion. Slave "seen"
        // latches prevent duplicate side effects during this response stage.
        // This cuts address/request -> arbiter/ready -> MMU fault into two clocks.
        always @(posedge clk)begin
            if(reset || !request)valid<=0;
            else if(!valid && bus_ready)begin valid<=1;failed<=bus_error;data<=bus_read_data;end
        end
        assign ready=request && valid,error=valid && failed,read_data=data;
    end else begin: direct_response
        assign ready=bus_ready,error=bus_error,read_data=bus_read_data;
    end endgenerate
    uj11_mmu_cpu #(.BOOT_PC(BOOT_BIOS ? 16'o173774 : 16'o173000),.REGISTER_MICROCODE(1),
        .CMR_VALUE(RAM_BYTES>262144 ? 16'o000045 : 16'o001045),
        .MICROCODE(MICROCODE)) cpu(.clk(clk),.reset(cpu_reset),.halt_button(halt_button),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .peripheral_reset(peripheral_reset),.mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_lock(cpu_lock),
        .mem_address(address),.mem_write_data(write_data),.mem_ready(ready),.mem_error(error),.mem_read_data(read_data),
        .console_active(stopped),.console_halt(diagnostic_halt),.wait_active(diagnostic_wait),.waiting(),
        .retire(diagnostic_retire),.psw(psw),.ir(),.mmr0(),.mmr1(),.mmr2(),.mmr3(),.upc(),.uword(),.pc(),
        .debug_register_data(),.debug_register_address(5'b0));
    uj11_terak_bus #(.CLOCK_HZ(CLOCK_HZ),.QX_MECHANICAL_SPEEDUP(QX_MECHANICAL_SPEEDUP),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV),
        .CLEAR_WORDS(CLEAR_WORDS),.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.BOOT_BIOS(BOOT_BIOS),.RL_ENABLE(RL_ENABLE)) bus(
        .clk(clk),.reset(reset),.cold_reset(cold_reset),.storage_idle(storage_idle),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.video_clk(video_clk),.video_reset(video_reset),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(cpu_lock),.address(address),.write_data(write_data),
        .ready(bus_ready),.error(bus_error),.read_data(bus_read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .ps2_clock(ps2_clock),.ps2_data(ps2_data),.tvout(tvout),.audio(audio),.memory_initialized(memory_initialized),
        .cpu_start(cpu_start),.boot_complete(boot_complete),.sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),.sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso));
endmodule
