`timescale 1ns/1ps

// Isolated AM4/LSI-11M checkpoint for HC1200.  The original datapath and
// 56-bit MicROM use a board-native request/ready interface backed by external
// SPI FRAM; KL11, KW11-L, panel GPIO and retained ODT share the board bus.
module am4_hc1200_microcomp #(
	parameter integer FRAM_CLK_DIV = 2,
	parameter integer TICK_DIVISOR = 532000
) (
	output wire sd_cs_n, sd_sck, sd_mosi,
	input  wire sd_miso,
	input  wire res,
	input  wire rx,
	output wire tx,
	output wire gpio_mosi,
	input  wire gpio_miso,
	output wire gpio_msck,
	output wire gpio_mcs,
	output wire gpio_din,
	output wire gpio_ce,
	output wire gpio_clk,
	output wire gpio_rs,
	output wire gpio_blank,
	output wire gpio_reg_latch,
	input  wire [3:0] gpio_key_row
);
	wire clk;
	reg [1:0] reset_sync = 2'b11;
	wire reset = reset_sync[0];
	wire peripheral_reset;
	wire bus_request, bus_write, bus_ack;
	wire [1:0] bus_byte_select;
	wire [15:0] bus_address, bus_wdata, bus_rdata;
	wire virq, interrupt_strobe, interrupt_acknowledge, timer_event;
	wire [15:0] interrupt_vector;
	wire boot_complete, boot_rom_ena;
	wire [9:0] boot_rom_addr;
	wire [7:0] boot_rom_data;

	OSCH #(.NOM_FREQ("26.60")) internal_oscillator (
		.STDBY(1'b0), .OSC(clk)
	);

	always @(posedge clk or negedge res) begin
		if (!res) reset_sync <= 2'b11;
		else reset_sync <= {1'b0, reset_sync[1]};
	end

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(FRAM_CLK_DIV), .TICK_DIVISOR(TICK_DIVISOR),
		.SD_BOOT_ENABLE(1), .RK_SERVICE_ENABLE(1)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(bus_request), .write(bus_write),
		.byte_select(bus_byte_select), .address(bus_address),
		.wdata(bus_wdata), .rdata(bus_rdata), .acknowledge(bus_ack),
		.virq(virq), .interrupt_vector(interrupt_vector),
		.interrupt_strobe(interrupt_strobe),
		.interrupt_acknowledge(interrupt_acknowledge),
		.event_irq(timer_event),
		.uart_rx(rx), .uart_tx(tx), .spi_cs_n(gpio_mcs),
		.panel_key_rows(gpio_key_row), .panel_din(gpio_din),
		.panel_ce(gpio_ce), .panel_clk(gpio_clk), .panel_rs(gpio_rs),
		.panel_blank(gpio_blank), .panel_reg_latch(gpio_reg_latch),
		.spi_sck(gpio_msck), .spi_mosi(gpio_mosi), .spi_miso(gpio_miso),
		.sd_cs_n(sd_cs_n), .sd_sck(sd_sck), .sd_mosi(sd_mosi),
		.sd_miso(sd_miso), .boot_rom_ena(boot_rom_ena),
		.boot_rom_addr(boot_rom_addr), .boot_rom_data(boot_rom_data),
		.boot_complete(boot_complete)
	);

	am4_direct #(
		.MICROM_FILE("am4_mc.rom"), .BUS_TIMEOUT_BITS(11)
	) engine (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(timer_event), .vector_irq(virq),
		.bus_address(bus_address), .bus_write_data(bus_wdata),
		.bus_read_data(bus_rdata), .bus_request(bus_request),
		.bus_write(bus_write), .bus_byte_select(bus_byte_select),
		.bus_ready(bus_ack), .vector_data(interrupt_vector),
		.vector_ready(interrupt_acknowledge),
		.vector_request(interrupt_strobe), .boot_rom_ena(boot_rom_ena),
		.boot_rom_addr(boot_rom_addr), .boot_rom_data(boot_rom_data),
		.boot_select(2'b11)
	);

endmodule
