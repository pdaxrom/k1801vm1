// HC7000 SRAM bus with autonomous RK611/SERV and CPU/DMA arbitration.
// UJ11_MMU remains undefined; experimental MMU sources are in Git history.
`timescale 1ns/1ps

// Derived from the lsi11-fpga AM4 bus. Combines KL11, KW11-L and panel GPIO
// with USER/HALT SRAM. Separate firmware EBR supplies the ordinary PDP-11
// bootstrap, resident and cold walker. SD byte data/control is at
// 177500/177502. Cold startup releases the HALT ROM overlay before modules run.
module uj11_hc7000_bus #(
	parameter integer CLOCK_HZ = 24000000,
	parameter integer TICK_DIVISOR = 480000,
	parameter [0:0] UART_XO2 = 1,
	parameter [0:0] BOOT_ROM_ENABLE = 1,
	parameter [0:0] SD_BOOT_ENABLE = 0,
	parameter [0:0] RK_SERVICE_ENABLE = 0,
	parameter integer SD_SLOW_DIV = 60,
	parameter integer SD_FAST_DIV = 2
) (
	input  wire        clk,
	input  wire        rst,
	input  wire        peripheral_reset,
	input  wire        request,
	input  wire        write,
	input  wire [1:0]  byte_select,

	input  wire bank, physical,
	input  wire [15:0] address,

	input  wire [15:0] wdata,
	input  wire        instruction_fetch,

	output wire [15:0] rdata,
	output wire        acknowledge,
	output wire        virq,
	output wire debug_block,
	output wire [15:0] interrupt_vector,
	output wire [2:0] interrupt_priority,
	input  wire        interrupt_strobe,
	output wire        interrupt_acknowledge,
	output reg         event_irq,
	input  wire        uart_rx,
	output wire        uart_tx,
	input  wire [3:0]  panel_key_rows,
	output wire        panel_din,
	output wire        panel_ce,
	output wire        panel_clk,
	output wire        panel_rs,
	output wire        panel_blank,
	output wire        panel_reg_latch,
	input wire power_on,
	output wire memory_initialized,
	output wire [19:0] sram_address,
	inout wire [15:0] sram_data,
	output wire sram_ce_n, sram_oe_n, sram_we_n, sram_lb_n, sram_ub_n,
	output wire        sd_cs_n,
	output wire        sd_sck,
	output wire        sd_mosi,
	input  wire        sd_miso,
	output wire        boot_rom_ena,
	output wire [9:0]  boot_rom_addr,
	output wire [1:0]  boot_rom_write,
	input  wire [15:0] boot_rom_data,
	output reg         boot_complete,
	output wire        host_miso,
	output wire        host_miso_oe
);
	localparam [15:0] KL11_BASE = 16'o177560;
	localparam [15:0] LTC_CSR = 16'o177546;
	// 166000 starts DEC's recommended customer/third-party CSR range.  Keep
	// 177750 free for the processor-owned DCJ11 MAINT register.
	localparam [15:0] PANEL_BASE = 16'o166000;
	localparam [15:0] SD_BASE = 16'o177500;
	localparam [15:0] BOOT_BASE = 16'o004000;
	localparam [15:0] BOOT_LAST = 16'o004777;
	localparam [15:0] RK_BASE = 16'o177440;

	wire [15:0] word_address = {address[15:1], 1'b0};
	reg boot_overlay_active;
	reg boot_release_armed;
	reg boot_release_wait;
	wire rk_irq_pending, rk_ready;
	reg rk_local_vector_ack;
	assign debug_block=1'b0;

	// Only the cold HALT PC/PSW pair is supplied by the small overlay.
	// USER bootstrap and legacy USER ODT words now reside in FRAM.
	wire local_boot_selected = bank && !physical && BOOT_ROM_ENABLE &&
		boot_overlay_active && !write && word_address[15:2]==0;

	wire boot_program_selected = bank && !physical && BOOT_ROM_ENABLE && SD_BOOT_ENABLE &&
		boot_overlay_active && !write && word_address[15:11]==5'd2;

	wire boot_selected = local_boot_selected || boot_program_selected;

	wire io_page = &address[15:13];

	wire program_selected=boot_program_selected;
	wire cpu_io_page=io_page && !physical;

	wire uart_selected = cpu_io_page &&

		word_address[12:0] >= KL11_BASE[12:0] &&
		word_address[12:0] <= 13'o17566;

	wire ltc_selected = cpu_io_page && word_address[12:0] == LTC_CSR[12:0];
	wire panel_selected = cpu_io_page && word_address[12:0] == PANEL_BASE[12:0];
	// Read-only board identification, KDJ11-A field layout: module1, no FPA,
	// Q-bus, HALT trap option and power-good. No MMU capability is advertised.
	wire maint_selected = cpu_io_page && word_address[12:0] == 13'o17750;
	wire sd_selected = cpu_io_page && SD_BOOT_ENABLE &&

		(word_address[12:0] == SD_BASE[12:0] ||
		 word_address[12:0] == SD_BASE[12:0] + 2);

	wire rk_selected = cpu_io_page && RK_SERVICE_ENABLE &&
		word_address[12:5] == RK_BASE[12:5];
	wire fram_selected=physical || (!io_page && !boot_selected);
	wire firmware_selected=program_selected;
	wire immediate_io_response=word_address[12:1]==LTC_CSR[12:1] ||
		word_address[12:1]==PANEL_BASE[12:1] || word_address[12:0]==13'o17750;
	wire uart_strobe = request && uart_selected;
	wire sd_strobe = request && sd_selected;
	wire [15:0] uart_rdata;
	wire uart_ack;
	wire uart_rts;
	wire tx_irq, rx_irq, tx_irq_ack, rx_irq_ack;
	wire vic_strobe = interrupt_strobe && !rk_irq_pending && !rk_local_vector_ack;
	wire uart_irq = rx_irq || tx_irq;
	wire [15:0] vic_data = rx_irq ? 16'o000060 : 16'o000064;
	wire vic_ack = vic_strobe && uart_irq;
	assign rx_irq_ack = vic_ack && rx_irq;
	assign tx_irq_ack = vic_ack && !rx_irq && tx_irq;
	wire fram_request = request && fram_selected;
	wire [15:0] fram_rdata;
	wire fram_ready, fram_error, fram_busy;
	wire [15:0] sd_rdata;
	wire sd_ready, sd_error;
	wire [15:0] rk_rdata;
	reg boot_ack;
	reg boot_program_ack;
	reg [1:0] boot_rom_phase;
	wire [15:0] boot_program_word=boot_rom_data;
	wire tick_terminal;
	uj11_tick #(.DIVISOR(TICK_DIVISOR)) timebase(clk,rst || peripheral_reset,tick_terminal);
	reg timer_done;
	reg timer_ie;
	wire [7:0] panel_output;
	reg [15:0] local_rdata;

    wire dma_request,dma_write,dma_ready;
    wire [15:0] dma_address,dma_data;
    wire memory_request,memory_write,memory_ready;
    wire [19:0] memory_address;
    wire [1:0] memory_lanes;
    wire [15:0] memory_data;
    uj11_sram_arbiter arbiter(.clk(clk),.reset(rst || peripheral_reset),
        .cpu_request(fram_request),.cpu_write(write),.cpu_address({4'b0,bank,address[15:1]}),
        .cpu_lanes(write ? byte_select : 2'b11),.cpu_data(wdata),.cpu_ready(fram_ready),
        .dma_request(dma_request),.dma_write(dma_write),.dma_address(dma_address),
        .dma_data(dma_data),.dma_ready(dma_ready),
        .request(memory_request),.write(memory_write),.address(memory_address),
        .lanes(memory_lanes),.data(memory_data),.ready(memory_ready));
    uj11_sram guest_memory (
        .clk(clk), .power_on(power_on), .reset(rst || peripheral_reset),
        .initialized(memory_initialized), .request(memory_request), .write(memory_write),
        .address(memory_address),.byte_enable(memory_lanes),.write_data(memory_data),
        .read_data(fram_rdata), .ready(memory_ready),
        .sram_address(sram_address), .sram_data(sram_data),
        .sram_ce_n(sram_ce_n), .sram_oe_n(sram_oe_n), .sram_we_n(sram_we_n),
        .sram_lb_n(sram_lb_n), .sram_ub_n(sram_ub_n)
    );
    assign fram_error=1'b0;
    assign fram_busy=1'b0;
    uj11_disk #(.CLOCK_HZ(CLOCK_HZ),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV)) disk(
        .clk(clk),.reset(rst || peripheral_reset),
        .rk_request(request && rk_selected),.rk_write(write),.rk_address(word_address[4:1]),
        .rk_lanes(byte_select),.rk_wdata(wdata),.rk_rdata(rk_rdata),.rk_ready(rk_ready),
        .rk_irq(rk_irq_pending),.rk_irq_ack(interrupt_strobe && rk_irq_pending && !rk_local_vector_ack),
        .sd_request(sd_strobe),.sd_write(write),.sd_byte(byte_select!=2'b11),
        .sd_address(address[1:0]),.sd_wdata(wdata),.sd_rdata(sd_rdata),.sd_ready(sd_ready),.sd_error(sd_error),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .dma_request(dma_request),.dma_write(dma_write),.dma_address(dma_address),.dma_data(dma_data),
        .dma_ready(dma_ready),.dma_rdata(fram_rdata));

	// Fixed initial HALT vector. Every USER address reads ordinary RAM.
	always @(*) local_rdata = word_address[1] ? 16'o000340 : 16'o010652;

	generate if (UART_XO2) begin : fixed_uart
	wbc_uart_xo2 #(.REFCLK(CLOCK_HZ)) console (
		.wb_clk_i(clk), .wb_rst_i(rst || peripheral_reset),
		.wb_adr_i(address[2:0]), .wb_dat_i(wdata),
		.wb_dat_o(uart_rdata), .wb_cyc_i(uart_strobe),
		.wb_we_i(write), .wb_stb_i(uart_strobe), .wb_ack_o(uart_ack),
		.tx_dat_o(uart_tx), .tx_cts_i(1'b0), .rx_dat_i(uart_rx),
		.rx_dtr_o(uart_rts), .tx_irq_o(tx_irq), .tx_ack_i(tx_irq_ack),
		.rx_irq_o(rx_irq), .rx_ack_i(rx_irq_ack)
	);
	end else begin : original_uart
	wbc_uart #(.REFCLK(CLOCK_HZ)) console (
		.wb_clk_i(clk), .wb_rst_i(rst || peripheral_reset),
		.wb_adr_i(address[2:0]), .wb_dat_i(wdata),
		.wb_dat_o(uart_rdata), .wb_cyc_i(uart_strobe),
		.wb_we_i(write), .wb_stb_i(uart_strobe), .wb_ack_o(uart_ack),
		.tx_dat_o(uart_tx), .tx_cts_i(1'b0), .rx_dat_i(uart_rx),
		.rx_dtr_o(uart_rts), .tx_irq_o(tx_irq), .tx_ack_i(tx_irq_ack),
		.rx_irq_o(rx_irq), .rx_ack_i(rx_irq_ack), .cfg_bdiv(16'd7),
		.cfg_nbit(2'b11), .cfg_nstp(1'b0), .cfg_pena(1'b0),
		.cfg_podd(1'b0)
	);
	end endgenerate

	wire [15:0] ltc_rdata = {8'b0, timer_done, timer_ie, 6'b0};
	wire [15:0] panel_rdata;
	// Decodes are mutually exclusive, including boot overlays and RK physical
	// copy cycles. Parallel masked buses avoid an eight-level priority chain.

	wire [15:0] small_rdata = (uart_rdata & {16{uart_selected}}) |
		(16'o000031 & {16{maint_selected}}) |
		(ltc_rdata & {16{ltc_selected}}) |
		(panel_rdata & {16{panel_selected}}) |
		(sd_rdata & {16{sd_selected}}) |
		(rk_rdata & {16{rk_selected}}) |
		(local_rdata & {16{local_boot_selected}});
	assign rdata = firmware_selected ? boot_program_word :

		fram_selected ? fram_rdata : small_rdata;

	assign acknowledge = rk_ready || uart_ack || (sd_ready && !sd_error) ||
		boot_ack || boot_program_ack || (fram_ready && !fram_error) ||
		(request && cpu_io_page && immediate_io_response);
	assign virq = rk_irq_pending || uart_irq;
	assign interrupt_vector = rk_irq_pending ? 16'o000210 : vic_data;
	assign interrupt_priority = rk_irq_pending ? 3'd5 : 3'd4;
	assign interrupt_acknowledge = rk_local_vector_ack || vic_ack;
	always @(posedge clk) begin
		if(rst || peripheral_reset || !interrupt_strobe)rk_local_vector_ack<=0;
		else if(rk_irq_pending)rk_local_vector_ack<=1;
	end

	// The bootstrap sets control bit 2 immediately before CLR PC.  Its first
	// read at address zero can then expose all RAM, including 024/026 and the
	// retained ODT words.
	always @(posedge clk) begin
		if (rst) begin
			boot_overlay_active <= 1;
			boot_release_armed <= 0;
			boot_release_wait <= 0;
			boot_complete <= !SD_BOOT_ENABLE;
		end else begin
			// Cold walker runs from FRAM before releasing HALT ROM.
			if (bank && !physical && request && write && word_address==16'o76)
				boot_overlay_active <= 0;
			if (SD_BOOT_ENABLE && sd_ready && write &&
				word_address == SD_BASE + 2 && wdata[2])
				boot_release_armed <= 1;

			if (boot_release_armed && !bank && !physical && request && !write && word_address == 0) begin

				boot_overlay_active <= 0;
				boot_release_armed <= 0;
				boot_release_wait <= 1;
			end
			if (boot_release_wait) begin
				boot_release_wait <= 0;
				boot_complete <= 1;
			end
		end
	end

	// A complete PDP-11 word comes from one synchronous firmware EBR.
	assign boot_rom_addr = {bank && word_address[10],bank && word_address[9],word_address[8:1]};
	assign boot_rom_write = 2'b0;
	assign boot_rom_ena = boot_rom_phase == 1 && !rst && !peripheral_reset;
	always @(posedge clk) begin
		if (rst || peripheral_reset) begin
			boot_rom_phase <= 0;
			boot_program_ack <= 0;
		end else case (boot_rom_phase)
			0: if (request && firmware_selected) boot_rom_phase <= 1;
			1: boot_rom_phase <= 2;
			2: begin
				boot_program_ack <= 1;
				boot_rom_phase <= 3;
			end
			3: if (!request) begin
				boot_program_ack <= 0;
				boot_rom_phase <= 0;
			end
		endcase
	end

	// The small reset overlay responds like a registered ROM.  Unmapped I/O and
	// illegal FRAM cycles deliberately receive no acknowledge, allowing AM4's
	// Q-bus timer to enter the original bus-error path.
	always @(posedge clk) begin
		if (rst || peripheral_reset)
			boot_ack <= 0;
		else
			boot_ack <= request && local_boot_selected && !boot_ack;
	end

	// Legacy HC1200 panel register.  The low byte reads the four keyboard rows;
	// high-byte bits 5:0 drive the panel and bits 7:6 provide a software-clocked
	// host data output and output enable on the TDO pad.
	// RGB and keyboard-column selection use that external shift register, so no
	// display framebuffer or keyboard scanner is required in the full FPGA.
	wire [3:0] hg_rows;
	uj11_hg_inputs hg_inputs(.clk(clk),.reset(rst || peripheral_reset),
		.output_enable(host_miso_oe),.rows(panel_key_rows),.filtered(hg_rows));
	uj11_panel panel_io (
		.clk(clk), .reset(rst || peripheral_reset),
		.write_enable(request && panel_selected && write && byte_select[1]),
		.rows(hg_rows), .write_data(wdata[15:8]),
		.pins(panel_output), .read_data(panel_rdata)
	);
	assign panel_din = panel_output[0];
	assign panel_ce = panel_output[1];
	assign panel_clk = panel_output[2];
	assign panel_rs = panel_output[3];
	assign panel_blank = panel_output[4];
	assign panel_reg_latch = panel_output[5];
	assign host_miso = panel_output[6];
	assign host_miso_oe = panel_output[7];

	// KW11-L-compatible line-time clock.  The counter is free-running, while
	// CSR bit 6 explicitly enables EVNT.  CSR bit 7 is the monitor/DONE latch;
	// writing it as zero clears DONE.  No guest-memory contents are inspected.
	always @(posedge clk) begin
		if (rst || peripheral_reset) begin
			event_irq <= 0;
			timer_done <= 1;
			timer_ie <= 0;
		end else begin
			event_irq <= 0;
			if (tick_terminal) begin
				timer_done <= 1;
				if (timer_ie)
					event_irq <= 1;
			end
			if (request && ltc_selected && write && byte_select[0]) begin
				timer_ie <= wdata[6];
				if (!wdata[7])
					timer_done <= 0;
				if (!wdata[6])
					event_irq <= 0;
				else if (wdata[7] && timer_done)
					event_irq <= 1;
			end
		end
	end

	wire unused_uart_rts = uart_rts;
	wire unused_fram_busy = fram_busy;
endmodule
