`timescale 1ns/1ps
// Full machine with external clocks. Board primitives are kept outside the SoC.
module bk_computer #(parameter COLOR = 0, PANEL_CLOCK_HZ = 24000000,
                     BIOS_CLOCK_HZ = 24000000) (
    input wire clk, clk48, video_clk, power_on, button_n,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n, sram_oe_n, sram_we_n, sram_lb_n, sram_ub_n,
    output wire sd_cs_n, sd_sck, sd_mosi,
    input wire sd_miso,
    output wire [5:0] tvout,
    output wire [1:0] audio,
    output wire uart_tx,
    output wire [8:0] seg_led_h, seg_led_l,
    output wire memory_initialized, rom_locked, boot_error, video_underrun,
    output wire [7:0] boot_stage
);
    wire cpu_p, cpu_n, raw_cpu_p, raw_cpu_n, hard_reset;
`ifdef SYNTHESIS
    DCCA cpu_positive(.CLKI(raw_cpu_p), .CE(1'b1), .CLKO(cpu_p));
    // Synplify maps the inverted edge to the slice/EBR clock polarity mux.
    // Both phases share one primary clock tree, so TRACE sees the half period.
    assign cpu_n = !cpu_p;
`else
    assign cpu_p = raw_cpu_p, cpu_n = raw_cpu_n;
`endif
    wire [2:0] requested_mode, active_mode, clock_request, display_mode;
    wire [2:0] model, boot_frequency;
    wire color;
    wire [15:0] video_register;
    wire full_reset_request;
    reg full_pending = 0, full_reset = 0, warm_reset = 1, locked_seen = 0;
    wire cycle, strobe, writing, acknowledge, bridge_busy, peripheral_reset, storage_idle, cpu_stall;
    always @(posedge clk) begin
        full_reset <= 0;
        if(power_on) begin full_pending <= 0; warm_reset <= 1; locked_seen <= 0; end
        else begin
            locked_seen <= rom_locked;
            if(full_reset_request) full_pending <= 1;
            if(!hard_reset) warm_reset <= 0;
            else if(!cycle && !strobe && !bridge_busy && storage_idle) warm_reset <= 1;
            // BOS expects the VM1 power-up sequence after BIOS hands over.
            // ROM/model/partition latches survive this CPU/peripheral reset.
            if(rom_locked && !locked_seen) warm_reset <= 1;
            if(full_pending && !cycle && !strobe && !bridge_busy && storage_idle) begin
                full_reset <= 1; full_pending <= 0;
            end
        end
    end
    bk_clock_mailbox frequency_mailbox(clk,clk48,power_on,
        rom_locked ? requested_mode : 3'b0,clock_request,active_mode,display_mode);
    wire reset = power_on || warm_reset || !memory_initialized;
    reg [2:0] cpu_reset_pipe = 7 /* synthesis syn_preserve=1 */;
    always @(posedge cpu_p or posedge reset)
        if(reset) cpu_reset_pipe <= 7; else cpu_reset_pipe <= {cpu_reset_pipe[1:0],1'b0};
    wire cpu_reset = cpu_reset_pipe[2];
    bk_panel #(.CLOCK_HZ(PANEL_CLOCK_HZ)) panel(clk, power_on, button_n, display_mode,
                                             requested_mode, hard_reset, seg_led_h, seg_led_l,
                                             boot_frequency,rom_locked && !locked_seen,full_reset_request);
    bk_cpu_clock clocks(clk48, power_on, clock_request, !cycle && !strobe && !bridge_busy,
                        raw_cpu_p, raw_cpu_n, active_mode, cpu_stall);
    wire [15:0] cpu_address, cpu_data, cpu_read, external_address, external_data, external_read;
    wire [1:0] cpu_lanes, external_lanes;
    reg [15:0] cpu_bank = 0;
    wire [15:0] external_bank;
    reg [2:0] model_meta = 2, model_sync = 2 /* synthesis syn_preserve=1 */;
    reg [15:0] vector_meta = 16'o60, vector_sync = 16'o60;
    reg locked_meta = 0, cpu_locked = 0 /* synthesis syn_preserve=1 */;
    wire system_write, system_read, external_write, irq_ack;
    wire keyboard_irq, key_held, stop;
    wire [15:0] keyboard_vector;
    reg [2:0] irq_sync = 0 /* synthesis syn_preserve=1 */;
    reg [1:0] held_sync = 0, stop_sync = 0 /* synthesis syn_preserve=1 */;
    reg [7:0] sys_input = 8'o370, sys_output = 8'o220;
    reg [15:0] ext_output = 16'o177777;
    reg ack_toggle = 0, ack_previous = 0;
    always @(posedge cpu_p or posedge cpu_reset) begin
        if(cpu_reset) begin
            irq_sync <= 0; held_sync <= 0; stop_sync <= 0;
            locked_meta <= 0; cpu_locked <= 0;
            cpu_bank <= 0; model_meta <= 2; model_sync <= 2; vector_meta <= 16'o60; vector_sync <= 16'o60;
            sys_input <= 8'o370; sys_output <= 8'o220; ext_output <= 16'o177777;
            ack_toggle <= 0; ack_previous <= 0;
        end else begin
            model_meta <= model; model_sync <= model_meta;
            locked_meta <= rom_locked; cpu_locked <= locked_meta;
            vector_meta <= keyboard_vector; vector_sync <= vector_meta;
            irq_sync <= {irq_sync[1:0], keyboard_irq};
            held_sync <= {held_sync[0],key_held}; stop_sync <= {stop_sync[0],stop};
            sys_input[6] <= !held_sync[1];
            if(system_write) begin
                sys_input[2] <= 1;
                if(model_sync >= 3 && cpu_lanes == 3 && cpu_data[11]) cpu_bank <= cpu_data;
                else if(cpu_lanes[0] && !(model_sync >= 3 && cpu_data[11])) sys_output <= cpu_data[7:0] & 8'o360;
            end else if(system_read) sys_input[2] <= 0;
            if(external_write) begin
                if(cpu_lanes[0]) ext_output[7:0] <= cpu_data[7:0];
                if(cpu_lanes[1]) ext_output[15:8] <= cpu_data[15:8];
            end
            ack_previous <= irq_ack;
            if(irq_ack && !ack_previous) ack_toggle <= !ack_toggle;
        end
    end
    bk_cpu cpu (
        .clk_p(cpu_p), .clk_n(cpu_n), .reset(cpu_reset), .virq(irq_sync[2]), .stop(stop_sync[1] && cpu_locked),
        .irq_vector(vector_sync), .external_input(16'o177777),
        .system_input({!cpu_locked ? 8'o200 : model_sync == 0 ? 8'o351 : model_sync >= 3 ? 8'o300 : 8'o200,sys_input}), .irq_ack(irq_ack), .peripheral_reset(peripheral_reset),
        .address(cpu_address), .write_data(cpu_data), .lanes(cpu_lanes), .cycle(cycle),
        .strobe(strobe), .write(writing), .read_data(cpu_read), .acknowledge(acknowledge),
        .system_write(system_write), .system_read(system_read), .external_write(external_write)
    );
    wire external_request, external_write_enable, external_ready;
    bk_wb_cdc bridge (
        .cpu_clk(cpu_p), .sys_clk(clk), .reset(reset), .cpu_cycle(cycle), .cpu_strobe(strobe),
        .cpu_write(writing), .cpu_address(cpu_address), .cpu_data(cpu_data), .cpu_lanes(cpu_lanes),
        .cpu_ack(acknowledge), .cpu_read(cpu_read), .cpu_busy(bridge_busy),
        .request(external_request), .write(external_write_enable), .address(external_address),
        .write_data(external_data), .lanes(external_lanes), .ready(external_ready), .read_data(external_read),
        .cpu_map_register(cpu_bank), .map_register(external_bank)
    );
    wire [15:0] boot_data, scroll;
    bk_boot_rom bootrom(clk, external_address[12:1], boot_data);
    wire key_valid, key_ready;
    wire [7:0] key_code;
    reg [1:0] init_sync = 0, ack_sync = 0 /* synthesis syn_preserve=1 */;
    reg ack_seen = 0;
    always @(posedge clk) begin
        if(reset) begin init_sync <= 3; ack_sync <= 0; ack_seen <= 0; end
        else begin
            init_sync <= {init_sync[0],peripheral_reset};
            ack_sync <= {ack_sync[0],ack_toggle}; ack_seen <= ack_sync[1];
        end
    end
    wire [8:0] scan_value;
    wire scan_pop;
    uj11_ps2_keyboard #(.CLOCK_HZ(24000000)) transport(clk, reset || init_sync[1], ps2_clock, ps2_data, scan_pop, scan_value);
    bk_keymap keymap(clk, reset || init_sync[1], scan_value[8], scan_value[7:0], scan_pop, key_valid, key_code, key_ready, key_held, stop,!rom_locked);
    wire cpu_mem_request, cpu_mem_write, cpu_mem_ready;
    wire [19:0] cpu_mem_address;
    wire [15:0] cpu_mem_data, mem_read;
    wire [1:0] cpu_mem_lanes;
    bk_system_bus #(.BIOS_CLOCK_HZ(BIOS_CLOCK_HZ)) bus (
        .clk(clk), .power_on(power_on || full_reset), .reset(reset), .peripheral_reset(init_sync[1]),
        .quiesce(hard_reset||full_pending),.storage_idle(storage_idle),.cpu_stall(cpu_stall),
        .request(external_request), .write(external_write_enable), .address(external_address),
        .write_data(external_data), .lanes(external_lanes), .ready(external_ready), .read_data(external_read),
        .mem_request(cpu_mem_request), .mem_write(cpu_mem_write), .mem_address(cpu_mem_address),
        .mem_data(cpu_mem_data), .mem_lanes(cpu_mem_lanes), .mem_ready(cpu_mem_ready), .mem_read(mem_read),
        .boot_data(boot_data), .key_valid(key_valid), .key_code(key_code), .key_ready(key_ready),
        .keyboard_irq(keyboard_irq), .keyboard_vector(keyboard_vector), .irq_ack(ack_sync[1] != ack_seen),
        .scroll(scroll), .locked(rom_locked), .boot_error(boot_error), .boot_stage(boot_stage),
        .sd_cs(sd_cs_n), .sd_sck(sd_sck), .sd_mosi(sd_mosi), .sd_miso(sd_miso),
        .map_register(external_bank), .model(model), .boot_frequency(boot_frequency), .color(color), .video_register(video_register)
    );
    wire video_request, video_ready;
    wire [19:0] video_address;
    bk_pal_video #(.COLOR(COLOR)) video (
        .clk(clk), .video_clk(video_clk), .reset(reset), .scroll(scroll),
        .dma_request(video_request), .dma_address(video_address), .dma_ready(video_ready),
        .dma_data(mem_read), .dac(tvout), .underrun(video_underrun),
        .model11(rom_locked && model >= 3), .color_mode(rom_locked && color),
        .page(video_register[15]), .palette(video_register[11:8])
    );
    wire mem_request, mem_write, mem_ready;
    wire [19:0] mem_address;
    wire [15:0] mem_data;
    wire [1:0] mem_lanes;
    bk_sram_arbiter arbiter (
        .clk(clk), .reset(reset), .cpu_request(cpu_mem_request), .cpu_write(cpu_mem_write),
        .cpu_address(cpu_mem_address), .cpu_data(cpu_mem_data), .cpu_lanes(cpu_mem_lanes), .cpu_ready(cpu_mem_ready),
        .video_request(video_request), .video_address(video_address), .video_ready(video_ready),
        .request(mem_request), .write(mem_write), .address(mem_address), .data(mem_data), .lanes(mem_lanes), .ready(mem_ready)
    );
    uj11_sram #(.CLEAR_WORDS(65536)) sram (
        .clk(clk), .power_on(power_on), .reset(reset && memory_initialized),
        .request(mem_request), .write(mem_write), .address(mem_address), .byte_enable(mem_lanes),
        .write_data(mem_data), .read_data(mem_read), .ready(mem_ready), .initialized(memory_initialized),
        .sram_address(sram_address), .sram_data(sram_data), .sram_ce_n(sram_ce_n), .sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n), .sram_lb_n(sram_lb_n), .sram_ub_n(sram_ub_n)
    );
    wire mono_audio;
    bk_audio sound(cpu_p,clk48,cpu_reset || peripheral_reset,external_write,cpu_lanes,cpu_data,
                   sys_output[6],mono_audio);
    assign audio = {2{mono_audio}};
    bk_boot_uart uart(clk, power_on, boot_stage, boot_error, uart_tx);
endmodule
