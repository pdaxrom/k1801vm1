`timescale 1ns/1ps

// Check the AM4-specific lane conversion and address decode around the shared
// SPI FRAM engine without relying on the CPU microprogram.
module tb_am4_fram_bus;
	reg clk = 0;
	reg reset = 1;
	reg request = 0;
	reg write = 0;
	reg [1:0] byte_select = 0;
	reg [15:0] address = 0;
	reg [15:0] write_data = 0;
	wire [15:0] read_data;
	wire acknowledge;
	wire virq, vector_ready, event_irq, uart_tx;
	wire [15:0] vector_data;
	wire spi_cs_n, spi_sck, spi_mosi, spi_miso;
	integer transactions;
	integer clocks;
	integer timer_clocks;

	always #5 clk = !clk;

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(128), .RK_SERVICE_ENABLE(1)
	) dut (
		.clk(clk), .rst(reset), .peripheral_reset(1'b0),
		.request(request), .write(write), .byte_select(byte_select),
		.address(address), .wdata(write_data), .rdata(read_data),
		.acknowledge(acknowledge), .virq(virq),
		.interrupt_vector(vector_data), .interrupt_strobe(1'b0),
		.interrupt_acknowledge(vector_ready), .event_irq(event_irq),
		.timer_enable(1'b1), .uart_rx(1'b1), .uart_tx(uart_tx),
		.spi_cs_n(spi_cs_n), .spi_sck(spi_sck), .spi_mosi(spi_mosi),
		.spi_miso(spi_miso), .sd_cs_n(), .sd_sck(), .sd_mosi(),
		.sd_miso(1'b1), .boot_rom_ena(), .boot_rom_addr(),
		.boot_rom_data(8'b0), .boot_complete()
	);

	spi_fram_model fram (
		.cs_n(spi_cs_n), .sck(spi_sck), .mosi(spi_mosi), .miso(spi_miso)
	);

	task automatic complete_cycle(
		input cycle_write,
		input [1:0] cycle_select,
		input [15:0] cycle_address,
		input [15:0] cycle_data,
		output [15:0] result
	);
	begin
		@(negedge clk);
		write = cycle_write;
		byte_select = cycle_select;
		address = cycle_address;
		write_data = cycle_data;
		request = 1;
		clocks = 0;
		while (!acknowledge && clocks < 1000) begin
			@(negedge clk);
			clocks = clocks + 1;
		end
		if (!acknowledge)
			$fatal(1, "AM4 FRAM bus timeout at %06o", cycle_address);
		result = read_data;
		request = 0;
		@(negedge clk);
	end
	endtask

	task automatic expect_no_ack(
		input cycle_write,
		input [1:0] cycle_select,
		input [15:0] cycle_address
	);
	begin
		@(negedge clk);
		write = cycle_write;
		byte_select = cycle_select;
		address = cycle_address;
		write_data = 16'hffff;
		request = 1;
		repeat (100) begin
			@(negedge clk);
			if (acknowledge)
				$fatal(1, "invalid AM4 cycle acknowledged at %06o", cycle_address);
		end
		request = 0;
		@(negedge clk);
	end
	endtask

	reg [15:0] result;
	initial begin
		repeat (4) @(negedge clk);
		reset = 0;

		transactions = fram.transaction_count;
		complete_cycle(0, 2'b11, 16'o000024, 0, result);
		if (result !== 16'o000100 || fram.transaction_count != transactions)
			$fatal(1, "reset overlay leaked to FRAM: data=%06o", result);

		complete_cycle(1, 2'b11, 16'o000200, 16'o012345, result);
		complete_cycle(0, 2'b11, 16'o000200, 0, result);
		if (result !== 16'o012345)
			$fatal(1, "AM4 FRAM word mismatch: %06o", result);

		complete_cycle(1, 2'b01, 16'o000202, 16'o000123, result);
		complete_cycle(1, 2'b10, 16'o000203, {8'o256, 8'b0}, result);
		complete_cycle(0, 2'b11, 16'o000203, 0, result);
		if (result !== 16'o127123)
			$fatal(1, "AM4 FRAM byte-lane/aligned-read mismatch: %06o", result);

		transactions = fram.transaction_count;
		complete_cycle(0, 2'b11, 16'o177440, 0, result);
		if (result !== 16'o000200 || fram.transaction_count != transactions)
			$fatal(1, "AM4 RK reset CS1 mismatch: %06o", result);
		complete_cycle(0, 2'b11, 16'o177452, 0, result);
		if (result !== 16'o100701 || fram.transaction_count != transactions)
			$fatal(1, "AM4 RK fixed DS mismatch: %06o", result);
		complete_cycle(1, 2'b11, 16'o177440, 16'o000300, result);
		complete_cycle(0, 2'b11, 16'o177440, 0, result);
		if (result !== 16'o000300)
			$fatal(1, "AM4 RK bank-one CS1 mismatch: %06o", result);
		complete_cycle(1, 2'b11, 16'o177450, 16'o000040, result);
		complete_cycle(0, 2'b11, 16'o177440, 0, result);
		if (result !== 16'o000200)
			$fatal(1, "AM4 RK controller clear did not restore DONE: %06o", result);
		complete_cycle(1, 2'b11, 16'o177440, 16'o002003, result);
		complete_cycle(0, 2'b11, 16'o177440, 0, result);
		if (result !== 16'o000200)
			$fatal(1, "AM4 RK PACK ACK did not complete: %06o", result);
		complete_cycle(1, 2'b11, 16'o177440, 16'o000001, result);
		complete_cycle(0, 2'b11, 16'o177440, 0, result);
		if (result !== 16'o000200)
			$fatal(1, "AM4 RK NOP did not complete: %06o", result);

		transactions = fram.transaction_count;
		expect_no_ack(0, 2'b11, 16'o177000);
		if (fram.transaction_count != transactions)
			$fatal(1, "unmapped I/O access reached FRAM");

		expect_no_ack(1, 2'b11, 16'o000201);
		if ({fram.memory[17'o000201], fram.memory[17'o000200]} !== 16'o012345)
			$fatal(1, "odd word write modified FRAM");

		if (event_irq)
			$fatal(1, "AM4 timer fired before vector 100 was installed");
		complete_cycle(1, 2'b11, 16'o000100, 16'o001234, result);
		timer_clocks = 0;
		while (!event_irq && timer_clocks < 200) begin
			@(negedge clk);
			timer_clocks = timer_clocks + 1;
		end
		if (!event_irq)
			$fatal(1, "AM4 timer did not start after vector 100 was installed: armed=%b counter=%0d complete=%b",
				dut.timer_armed, dut.tick_counter, dut.boot_complete);

		$display("PASS: AM4 reset overlay, FRAM/RK banks, byte lanes, timer arm, odd and I/O isolation");
		$finish;
	end

	initial begin
		#500000;
		$fatal(1, "AM4 FRAM bus test timeout");
	end
endmodule
