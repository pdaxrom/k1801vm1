`timescale 1ns/1ps

// HC1200 system bus for the direct-bus AM4 checkpoint.  It combines KL11, a
// KW11-L-compatible clock and panel GPIO with bank-zero SPI FRAM.  During SD
// boot, a second MicROM port supplies an ordinary PDP-11 bootstrap and a byte
// SPI service at 177500/2.  Six reset/ODT words remain available on failure;
// the complete overlay disappears before a successful transfer to address 0.
module am4_hc1200_cpu11_bus #(
	parameter integer CLOCK_HZ = 29560000,
	parameter integer TICK_DIVISOR = 591200,
	parameter integer UART_XO2 = 1,
	parameter integer FRAM_CLK_DIV = 1,
	parameter integer BOOT_ROM_ENABLE = 1,
	parameter integer SD_BOOT_ENABLE = 0,
	parameter integer RK_SERVICE_ENABLE = 0,
	parameter integer SD_SLOW_DIV = 68,
	parameter integer SD_FAST_DIV = 2
) (
	input  wire        clk,
	input  wire        rst,
	input  wire        peripheral_reset,
	input  wire        request,
	input  wire        write,
	input  wire [1:0]  byte_select,
	input  wire [15:0] address,
	input  wire [15:0] wdata,
	output wire [15:0] rdata,
	output wire        acknowledge,
	output wire        virq,
	output wire [15:0] interrupt_vector,
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
	output wire        spi_cs_n,
	output wire        spi_sck,
	output wire        spi_mosi,
	input  wire        spi_miso,
	output wire        sd_cs_n,
	output wire        sd_sck,
	output wire        sd_mosi,
	input  wire        sd_miso,
	output wire        boot_rom_ena,
	output wire [9:0]  boot_rom_addr,
	input  wire [7:0]  boot_rom_data,
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
	localparam [15:0] RK_LAST = 16'o177476;
	localparam [15:0] RK_CS1 = 16'o177440;
	localparam [15:0] RK_CS2 = 16'o177450;
	localparam [15:0] RK_DS = 16'o177452;
	localparam [15:0] SERVICE_BASE = 16'o160000;
	localparam [15:0] SERVICE_RTI = 16'o160476;
	localparam [9:0] SERVICE_FRAGMENT_BASE = 10'd640;
	localparam integer TICK_WIDTH = TICK_DIVISOR > 1 ? $clog2(TICK_DIVISOR) : 1;

	wire [15:0] word_address = {address[15:1], 1'b0};
	reg boot_overlay_active;
	reg boot_release_armed;
	reg boot_release_wait;
	reg rk_service_pending;
	reg rk_service_active;
	reg rk_service_release;
	reg rk_local_vector_ack;
	reg rk_irq_pending;
	reg rk_cs1_initialized;
	reg rk_interrupt_enable;
	reg rk_immediate_done;
	wire local_boot_selected = BOOT_ROM_ENABLE && boot_overlay_active && !write &&
		(word_address == 16'o000024 || word_address == 16'o000026 ||
		 word_address == 16'o000100 || word_address == 16'o000102 ||
		 word_address == 16'o000104 || word_address == 16'o000106);
	wire boot_program_selected = BOOT_ROM_ENABLE && SD_BOOT_ENABLE &&
		boot_overlay_active && !write && word_address >= BOOT_BASE &&
		word_address <= BOOT_LAST;
	wire boot_selected = local_boot_selected || boot_program_selected;
	// 160000..160777 share one seven-bit prefix.  Bit 8 selects the compact
	// extension; the service never branches into its unused upper aliases.
	wire service_program_selected = RK_SERVICE_ENABLE && rk_service_active &&
		!write && word_address[15:9] == SERVICE_BASE[15:9];
	wire service_extension_selected = service_program_selected && word_address[8];
	wire program_selected = boot_program_selected || service_program_selected;
	wire uart_selected = word_address >= KL11_BASE &&
		word_address <= 16'o177566;
	wire ltc_selected = word_address == LTC_CSR;
	wire panel_selected = word_address == PANEL_BASE;
	wire sd_selected = SD_BOOT_ENABLE &&
		(word_address == SD_BASE || word_address == SD_BASE + 2);
	wire rk_selected = RK_SERVICE_ENABLE &&
		word_address[15:5] == RK_BASE[15:5];
	wire rk_cs1_selected = rk_selected && word_address[4:1] == 4'o0;
	wire rk_cs2_selected = rk_selected && word_address[4:1] == 4'o4;
	wire rk_ds_selected = rk_selected && word_address[4:1] == 4'o5;
	wire rk_fixed_selected = rk_ds_selected ||
		(rk_cs1_selected && !write &&
		 (!rk_cs1_initialized || rk_immediate_done));
	wire rk_fram_selected = rk_selected && !rk_fixed_selected;
	wire io_page = address[15:13] == 3'b111;
	wire guest_fram_selected = !io_page && !boot_selected;
	wire fram_selected = guest_fram_selected || rk_fram_selected;
	wire uart_strobe = request && uart_selected;
	wire sd_strobe = request && sd_selected;
	wire [15:0] uart_rdata;
	wire uart_ack;
	wire uart_rts;
	wire tx_irq, rx_irq, tx_irq_ack, rx_irq_ack;
	wire vic_strobe = interrupt_strobe && !rk_service_pending &&
		!rk_irq_pending && !rk_local_vector_ack;
	wire uart_irq = rx_irq || tx_irq;
	wire [15:0] vic_data = rx_irq ? 16'o000060 : 16'o000064;
	wire vic_ack = vic_strobe && uart_irq;
	assign rx_irq_ack = vic_ack && rx_irq;
	assign tx_irq_ack = vic_ack && !rx_irq && tx_irq;
	wire fram_request = request && fram_selected;
	wire fram_byte_access = write && byte_select != 2'b11;
	wire [15:0] fram_address = rk_fram_selected ?
		{11'b0, word_address[4:0]} : write ? address : word_address;
	wire [15:0] fram_wdata = byte_select == 2'b10 ?
		{8'b0, wdata[15:8]} : wdata;
	wire [15:0] fram_rdata;
	wire fram_ready, fram_error, fram_busy;
	wire [15:0] sd_rdata;
	wire sd_ready, sd_error, sd_busy;
	reg boot_ack;
	reg boot_program_ack;
	reg [2:0] boot_rom_phase;
	reg [15:0] boot_program_word;
	reg [TICK_WIDTH-1:0] tick_counter;
	reg timer_done;
	reg timer_ie;
	reg [7:0] panel_output;
	reg [15:0] local_rdata;

	spi_fram_guest_ram #(.CLK_DIV(FRAM_CLK_DIV)) guest_memory (
		.clk(clk), .rst(rst || peripheral_reset),
		.req(fram_request), .write(write),
		.byte_access(fram_byte_access), .bank(rk_fram_selected),
		.address(fram_address), .wdata(fram_wdata),
		.rdata(fram_rdata), .ready(fram_ready), .error(fram_error),
		.busy(fram_busy), .spi_cs_n(spi_cs_n), .spi_sck(spi_sck),
		.spi_mosi(spi_mosi), .spi_miso(spi_miso)
	);

	spi_byte_service #(
		.SLOW_DIV(SD_SLOW_DIV), .FAST_DIV(SD_FAST_DIV)
	) byte_sd (
		.clk(clk), .rst(rst || peripheral_reset), .req(sd_strobe),
		.write(write), .byte_access(byte_select != 2'b11),
		.address(address[1:0]), .wdata(wdata), .rdata(sd_rdata),
		.ready(sd_ready), .error(sd_error), .busy(sd_busy),
		.cs_n(sd_cs_n), .sck(sd_sck), .mosi(sd_mosi), .miso(sd_miso)
	);

	// This ROM is deliberately logic, not EBR: the 56x1024 AM4 MicROM already
	// consumes the complete seven-EBR budget of LCMXO2-1200HC.
	always @(*) begin
		case (word_address)
			16'o000024: local_rdata = SD_BOOT_ENABLE ?
				BOOT_BASE : 16'o000100;
			16'o000026: local_rdata = 16'o000000;
			16'o000100: local_rdata = 16'o012706;
			16'o000102: local_rdata = 16'o004000;
			16'o000104: local_rdata = 16'o000000;
			16'o000106: local_rdata = 16'o000774;
			default:    local_rdata = 16'o000000;
		endcase
	end

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
	wire [15:0] panel_rdata = {panel_output, panel_key_rows, 4'b0};
	assign rdata = uart_selected ? uart_rdata :
		ltc_selected ? ltc_rdata :
		panel_selected ? panel_rdata :
		sd_selected ? sd_rdata :
		rk_fixed_selected ? (rk_ds_selected ? 16'o100701 : 16'o000200) :
		local_boot_selected ? local_rdata :
		program_selected ? boot_program_word : fram_rdata;
	assign acknowledge = uart_ack || (request && ltc_selected) ||
		(request && panel_selected) ||
		(sd_ready && !sd_error) || boot_ack ||
		boot_program_ack || (request && rk_fixed_selected) ||
		(fram_ready && !fram_error);
	assign virq = rk_service_pending || rk_irq_pending || uart_irq;
	assign interrupt_vector = rk_local_vector_ack ?
		(rk_service_active ? SERVICE_BASE : 16'o000210) : vic_data;
	assign interrupt_acknowledge = rk_local_vector_ack || vic_ack;

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
			if (SD_BOOT_ENABLE && sd_ready && write &&
				word_address == SD_BASE + 2 && wdata[2])
				boot_release_armed <= 1;
			if (boot_release_armed && request && !write && word_address == 0) begin
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

	// Consecutive phases present the next fragment while capturing the previous
	// synchronous DP8KC result.  Ordinary words use three seven-bit fragments;
	// compact extension words use three five-bit fields plus a constant bit 15.
	wire [9:0] boot_fragment_base = {2'b0, word_address[8:1]} +
		{1'b0, word_address[8:1], 1'b0};
	wire [1:0] boot_fragment_plane = boot_rom_phase == 1 ? 2'd0 :
		boot_rom_phase == 2 ? 2'd1 : 2'd2;
	wire [9:0] service_fragment_address = {
		1'b1,
		service_extension_selected || |boot_fragment_plane,
		service_extension_selected || ~boot_fragment_plane[0] ||
			boot_fragment_plane[1],
		service_extension_selected ? boot_fragment_plane :
			word_address[7:6],
		word_address[5:1]
	};
	assign boot_rom_ena = boot_rom_phase != 0 && boot_rom_phase != 6;
	assign boot_rom_addr = service_program_selected ?
		service_fragment_address : boot_fragment_base + boot_fragment_plane;
	always @(posedge clk) begin
		if (rst || peripheral_reset) begin
			boot_rom_phase <= 0;
			boot_program_word <= 0;
			boot_program_ack <= 0;
		end else case (boot_rom_phase)
			0: if (request && program_selected) begin
				boot_rom_phase <= 1;
			end
			1: boot_rom_phase <= 2;
			2: begin
				if (service_extension_selected)
					boot_program_word[4:0] <= boot_rom_data[6:2];
				else
					boot_program_word[6:0] <= boot_rom_data[6:0];
				boot_rom_phase <= 3;
			end
			3: begin
				if (service_extension_selected)
					boot_program_word[9:5] <= boot_rom_data[6:2];
				else
					boot_program_word[13:7] <= boot_rom_data[6:0];
				boot_rom_phase <= 4;
			end
			4: begin
				if (service_extension_selected) begin
					boot_program_word[14:10] <= boot_rom_data[6:2];
					boot_program_word[15] <= boot_rom_data[7];
				end else begin
					boot_program_word[15:14] <= boot_rom_data[1:0];
				end
				boot_program_ack <= 1;
				boot_rom_phase <= 6;
			end
			6: if (!request) begin
				boot_program_ack <= 0;
				boot_rom_phase <= 0;
			end
			default: boot_rom_phase <= 0;
		endcase
	end

	// Writable RK words live in the otherwise unused second FRAM bank.  Before
	// the first CS1 write (and after controller clear), CS1 reads as DONE.  The
	// only persistent RTL state is command/interrupt sequencing.
	always @(posedge clk) begin
		if (rst || peripheral_reset) begin
			rk_service_pending <= 0;
			rk_service_active <= 0;
			rk_service_release <= 0;
			rk_local_vector_ack <= 0;
			rk_irq_pending <= 0;
			rk_cs1_initialized <= 0;
			rk_interrupt_enable <= 0;
			rk_immediate_done <= 0;
		end else begin
			if (!interrupt_strobe) begin
				rk_local_vector_ack <= 0;
			end else if (!rk_local_vector_ack) begin
				if (rk_service_pending) begin
					rk_local_vector_ack <= 1;
					rk_service_pending <= 0;
					rk_service_active <= 1;
				end else if (rk_irq_pending) begin
					rk_local_vector_ack <= 1;
					rk_irq_pending <= 0;
				end
			end

			if (fram_ready && rk_cs1_selected && write && byte_select[0]) begin
				rk_cs1_initialized <= 1;
				rk_interrupt_enable <= wdata[6];
				rk_immediate_done <= 0;
				if (wdata[0]) begin
					if (wdata[5:2] == 4'o4) begin
						rk_service_pending <= 1;
						rk_irq_pending <= 0;
					end else if (wdata[5:1] == 5'o0 ||
						wdata[5:1] == 5'o1) begin
						// NOP and PACK ACK complete without media traffic.
						// RT-11 uses both while bringing up the RK611.
						rk_immediate_done <= 1;
						rk_irq_pending <= wdata[6];
					end
				end else begin
					if (!wdata[6])
						rk_irq_pending <= 0;
					else if (!rk_service_active && wdata[7])
						rk_irq_pending <= 1;
				end
			end

			if (fram_ready && rk_cs2_selected && write && byte_select[0] &&
				wdata[5]) begin
				rk_cs1_initialized <= 0;
				rk_interrupt_enable <= 0;
				rk_service_pending <= 0;
				rk_irq_pending <= 0;
				rk_immediate_done <= 0;
			end

			if (rk_service_active && boot_program_ack && request &&
				word_address == SERVICE_RTI)
				rk_service_release <= 1;
			if (rk_service_release && !request) begin
				rk_service_release <= 0;
				rk_service_active <= 0;
				if (rk_interrupt_enable)
					rk_irq_pending <= 1;
			end
		end
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
	always @(posedge clk) begin
		if (rst || peripheral_reset)
			panel_output <= 8'b00010010; // CE high, display blanked, host Z
		else if (request && panel_selected && write && byte_select[1])
			panel_output <= wdata[15:8];
	end
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
			tick_counter <= 0;
			event_irq <= 0;
			timer_done <= 1;
			timer_ie <= 0;
		end else begin
			event_irq <= 0;
			if (tick_counter == TICK_DIVISOR - 1) begin
				tick_counter <= 0;
				timer_done <= 1;
				if (timer_ie)
					event_irq <= 1;
			end else begin
				tick_counter <= tick_counter + 1'b1;
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
	wire unused_sd_busy = sd_busy;
endmodule
