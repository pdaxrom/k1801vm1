`timescale 1ns/1ps

// Fetch an ordinary PDP-11 image directly from SPI FRAM, then exercise word,
// byte and read-modify-write cycles through the complete AM4 data path.
module tb_am4_fram_execute;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire request, write_enable, ready;
	wire [1:0] byte_select;
	wire [15:0] address, write_data, read_data;
	wire virq, vector_request, vector_ready, event_irq, uart_tx;
	wire [15:0] vector_data;
	wire spi_cs_n, spi_sck, spi_mosi, spi_miso;
	integer clocks = 0;

	always #5 clk = !clk;

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(1000), .BOOT_ROM_ENABLE(0)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(request), .write(write_enable), .byte_select(byte_select),
		.address(address), .wdata(write_data), .rdata(read_data),
		.acknowledge(ready), .virq(virq), .interrupt_vector(vector_data),
		.interrupt_strobe(vector_request),
		.interrupt_acknowledge(vector_ready), .event_irq(event_irq),
		.uart_rx(1'b1), .uart_tx(uart_tx), .panel_key_rows(4'b0),
		.panel_din(), .panel_ce(), .panel_clk(), .panel_rs(),
		.panel_blank(), .panel_reg_latch(),
		.spi_cs_n(spi_cs_n), .spi_sck(spi_sck), .spi_mosi(spi_mosi),
		.spi_miso(spi_miso), .sd_cs_n(), .sd_sck(), .sd_mosi(),
		.sd_miso(1'b1), .boot_rom_ena(), .boot_rom_addr(),
		.boot_rom_data(8'b0), .boot_complete()
	);

	am4_direct #(
		.MICROM_FILE(MICROM_FILE), .BUS_TIMEOUT_BITS(9)
	) engine (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(event_irq), .vector_irq(virq),
		.bus_address(address), .bus_write_data(write_data),
		.bus_read_data(read_data), .bus_request(request),
		.bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_ready(ready), .vector_data(vector_data),
		.vector_ready(vector_ready), .vector_request(vector_request),
		.boot_rom_ena(1'b0), .boot_rom_addr(10'b0), .boot_rom_data(),
		.boot_select(2'b11)
	);

	spi_fram_model fram (
		.cs_n(spi_cs_n), .sck(spi_sck), .mosi(spi_mosi), .miso(spi_miso)
	);

	always @(posedge clk) begin
		if (!reset) clocks <= clocks + 1;
		if (clocks >= 100000)
			$fatal(1, "AM4 FRAM execute timeout: ma=%03h ir=%06o address=%06o",
				engine.ma, engine.ireg, address);
	end

	initial begin
		#1 $readmemh("build/am4_fram.hex", fram.memory);
		repeat (15) @(negedge clk);
		reset = 0;
		wait ({fram.memory[17'o000205], fram.memory[17'o000204]} == 16'o000001);
		repeat (100) @(negedge clk);
		if ({fram.memory[17'o000201], fram.memory[17'o000200]} !== 16'o012346)
			$fatal(1, "AM4 FRAM RMW mismatch: %06o",
				{fram.memory[17'o000201], fram.memory[17'o000200]});
		if (fram.memory[17'o000202] !== 8'o123 ||
			fram.memory[17'o000203] !== 8'o256)
			$fatal(1, "AM4 FRAM byte writes mismatch: %03o/%03o",
				fram.memory[17'o000202], fram.memory[17'o000203]);
		if (engine.cc6)
			$fatal(1, "AM4 FRAM transaction raised Q-bus timeout");
		if (fram.transaction_count < 20)
			$fatal(1, "AM4 execution did not traverse SPI FRAM");
		$display("PASS: AM4 fetched from SPI FRAM and executed word/byte/RMW cycles (%0d clocks, %0d SPI transactions)",
			clocks, fram.transaction_count);
		$finish;
	end
endmodule
