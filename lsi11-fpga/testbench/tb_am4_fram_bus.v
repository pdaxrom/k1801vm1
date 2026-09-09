`timescale 1ns/1ps

// Check the AM4-specific lane conversion and address decode around the shared
// SPI FRAM engine without relying on the CPU microprogram.
module tb_am4_fram_bus;
	parameter UART_XO2 = 1;
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
	wire panel_din, panel_ce, panel_clk, panel_rs, panel_blank;
	wire panel_reg_latch;
	wire host_miso, host_miso_oe;
	integer transactions;
	integer clocks;
	integer timer_clocks;

	always #5 clk = !clk;

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(128), .RK_SERVICE_ENABLE(1),
		.UART_XO2(UART_XO2)
	) dut (
		.clk(clk), .rst(reset), .peripheral_reset(1'b0),
		.request(request), .write(write), .byte_select(byte_select),
		.address(address), .wdata(write_data),
		.instruction_fetch(1'b0), .rdata(read_data),
		.acknowledge(acknowledge), .virq(virq),
		.interrupt_vector(vector_data), .interrupt_strobe(1'b0),
		.interrupt_acknowledge(vector_ready), .event_irq(event_irq),
		.uart_rx(1'b1), .uart_tx(uart_tx),
		.panel_key_rows(4'b1010), .panel_din(panel_din),
		.panel_ce(panel_ce), .panel_clk(panel_clk), .panel_rs(panel_rs),
		.panel_blank(panel_blank), .panel_reg_latch(panel_reg_latch),
		.spi_cs_n(spi_cs_n), .spi_sck(spi_sck), .spi_mosi(spi_mosi),
		.spi_miso(spi_miso), .sd_cs_n(), .sd_sck(), .sd_mosi(),
		.sd_miso(1'b1), .boot_rom_ena(), .boot_rom_addr(),
		.boot_rom_data(8'b0), .boot_complete(),
		.host_miso(host_miso), .host_miso_oe(host_miso_oe)
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
		// The CPU samples ready on a rising edge. Keep request asserted
		// through that edge, including the original UART's registered ack.
		do begin
			@(posedge clk);
			clocks = clocks + 1;
		end while (!acknowledge && clocks < 1000);
		if (!acknowledge)
			$fatal(1, "AM4 FRAM bus timeout at %06o", cycle_address);
		result = read_data;
		@(negedge clk);
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

		// Bank-one byte writes must use exactly the same lanes as guest RAM.
		complete_cycle(1, 2'b11, 16'o177442, 16'h1234, result);
		complete_cycle(1, 2'b10, 16'o177443, 16'hab00, result);
		complete_cycle(0, 2'b11, 16'o177442, 0, result);
		if (result !== 16'hab34)
			$fatal(1, "RK high byte overwrote low byte: %04x", result);
		complete_cycle(1, 2'b01, 16'o177442, 16'h00cd, result);
		complete_cycle(0, 2'b11, 16'o177443, 0, result);
		if (result !== 16'habcd)
			$fatal(1, "RK low byte/aligned read mismatch: %04x", result);

		// Odd-address UART writes must acknowledge without changing low CSRs
		// or transmitting a byte. Check both RX and TX interrupt enables.
		complete_cycle(1, 2'b11, 16'o177560, 16'o100, result);
		complete_cycle(1, 2'b10, 16'o177561, 0, result);
		complete_cycle(0, 2'b11, 16'o177560, 0, result);
		if (!result[6]) $fatal(1, "UART high byte cleared RX IE");
		complete_cycle(1, 2'b11, 16'o177564, 16'o100, result);
		complete_cycle(1, 2'b10, 16'o177565, 0, result);
		complete_cycle(0, 2'b11, 16'o177564, 0, result);
		if (!result[6]) $fatal(1, "UART high byte cleared TX IE");
		complete_cycle(1, 2'b10, 16'o177567, 0, result);
		repeat (20) begin
			@(negedge clk);
			if (!uart_tx) $fatal(1, "UART high byte started transmission");
		end
		complete_cycle(1, 2'b01, 16'o177564, 0, result);
		complete_cycle(0, 2'b11, 16'o177564, 0, result);
		if (result[6]) $fatal(1, "UART low byte failed to clear TX IE");

		transactions = fram.transaction_count;
		expect_no_ack(0, 2'b11, 16'o177000);
		if (fram.transaction_count != transactions)
			$fatal(1, "unmapped I/O access reached FRAM");

		expect_no_ack(1, 2'b11, 16'o000201);
		if ({fram.memory[17'o000201], fram.memory[17'o000200]} !== 16'o012345)
			$fatal(1, "odd word write modified FRAM");

		if ({panel_reg_latch, panel_blank, panel_rs, panel_clk,
			panel_ce, panel_din} !== 6'b010010)
			$fatal(1, "AM4 panel reset state mismatch");
		if (host_miso_oe !== 1'b0)
			$fatal(1, "AM4 host output enabled after reset");
		complete_cycle(0, 2'b11, 16'o166000, 0, result);
		if (result !== 16'h12a0)
			$fatal(1, "AM4 panel initial read mismatch: %04x", result);
		complete_cycle(1, 2'b10, 16'o166001, 16'hed00, result);
		if ({panel_reg_latch, panel_blank, panel_rs, panel_clk,
			panel_ce, panel_din} !== 6'b101101)
			$fatal(1, "AM4 panel output write mismatch");
		if ({host_miso_oe, host_miso} !== 2'b11)
			$fatal(1, "AM4 host output write mismatch");
		complete_cycle(0, 2'b11, 16'o166000, 0, result);
		if (result !== 16'heda0)
			$fatal(1, "AM4 panel readback mismatch: %04x", result);

		// Installing vector 100 has no side effect on the line clock.
		complete_cycle(1, 2'b11, 16'o000100, 16'o001234, result);
		repeat (140) @(negedge clk);
		if (event_irq)
			$fatal(1, "AM4 timer depends on vector 100 contents");
		complete_cycle(0, 2'b11, 16'o177546, 0, result);
		if (result !== 16'o000200)
			$fatal(1, "AM4 KW11-L reset CSR mismatch: %06o", result);
		complete_cycle(1, 2'b11, 16'o177546, 16'o000100, result);
		timer_clocks = 0;
		while (!event_irq && timer_clocks < 200) begin
			@(negedge clk);
			timer_clocks = timer_clocks + 1;
		end
		if (!event_irq)
			$fatal(1, "AM4 KW11-L did not interrupt after IE: counter=%0d",
				dut.tick_counter);
		complete_cycle(0, 2'b11, 16'o177546, 0, result);
		if (result !== 16'o000300)
			$fatal(1, "AM4 KW11-L active CSR mismatch: %06o", result);
		complete_cycle(1, 2'b11, 16'o177546, 0, result);
		complete_cycle(0, 2'b11, 16'o177546, 0, result);
		if (result !== 0)
			$fatal(1, "AM4 KW11-L clear mismatch: %06o", result);

		$display("PASS: AM4 reset overlay, FRAM/RK banks, panel GPIO, KW11-L, byte lanes, odd and I/O isolation");
		$finish;
	end

	initial begin
		#500000;
		$fatal(1, "AM4 FRAM bus test timeout");
	end
endmodule
