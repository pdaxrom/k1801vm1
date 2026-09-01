`timescale 1ns/1ps

// Exercise the resource-fit path: a microasm11 PDP-11 bootstrap is read from
// spare AM4 MicROM bits, initializes SDHC through the byte SPI service, copies
// LBA0/1 to FRAM, removes its overlay, and enters address zero.
module tb_am4_sd_boot_execute;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	reg fail_boot = 0;
	wire peripheral_reset;
	wire request, write_enable, ready;
	wire instruction_fetch;
	wire [1:0] byte_select;
	wire [15:0] address, write_data, read_data;
	wire virq, vector_request, vector_ready, timer_event, uart_tx;
	wire [15:0] vector_data;
	wire fram_cs_n, fram_sck, fram_mosi, fram_miso;
	wire sd_cs_n, sd_sck, sd_mosi, sd_miso;
	wire boot_complete, boot_rom_ena;
	wire [9:0] boot_rom_addr;
	wire [7:0] boot_rom_data;
	integer clocks = 0;
	integer i;
	reg trace_rom = 0;

	always #5 clk = !clk;
	initial trace_rom = $test$plusargs("TRACE_ROM");

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(200), .SD_BOOT_ENABLE(1),
		.SD_SLOW_DIV(68), .SD_FAST_DIV(2)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(request), .write(write_enable), .byte_select(byte_select),
		.address(address), .wdata(write_data),
		.instruction_fetch(instruction_fetch), .rdata(read_data),
		.acknowledge(ready), .virq(virq), .interrupt_vector(vector_data),
		.interrupt_strobe(vector_request),
		.interrupt_acknowledge(vector_ready), .event_irq(timer_event),
		.uart_rx(1'b1), .uart_tx(uart_tx), .panel_key_rows(4'b0),
		.panel_din(), .panel_ce(), .panel_clk(), .panel_rs(),
		.panel_blank(), .panel_reg_latch(),
		.spi_cs_n(fram_cs_n), .spi_sck(fram_sck),
		.spi_mosi(fram_mosi), .spi_miso(fram_miso),
		.sd_cs_n(sd_cs_n), .sd_sck(sd_sck), .sd_mosi(sd_mosi),
		.sd_miso(sd_miso), .boot_rom_ena(boot_rom_ena),
		.boot_rom_addr(boot_rom_addr), .boot_rom_data(boot_rom_data),
		.boot_complete(boot_complete)
	);

	am4_direct #(
		.MICROM_FILE(MICROM_FILE), .BOOTROM_FILE("build/am4_bootrom.rom"),
		.BUS_TIMEOUT_BITS(11)
	) engine (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(1'b0), .vector_irq(virq),
		.bus_address(address), .bus_write_data(write_data),
		.bus_read_data(read_data), .bus_request(request),
		.bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_instruction_fetch(instruction_fetch),
		.bus_ready(ready), .vector_data(vector_data),
		.vector_ready(vector_ready), .vector_request(vector_request),
		.boot_rom_ena(boot_rom_ena), .boot_rom_addr(boot_rom_addr),
		.boot_rom_data(boot_rom_data), .boot_select(2'b11)
	);

	spi_fram_model fram (
		.cs_n(fram_cs_n), .sck(fram_sck), .mosi(fram_mosi), .miso(fram_miso)
	);

	spi_sd_model card (
		.cs_n(sd_cs_n), .sck(sd_sck), .mosi(sd_mosi), .miso(sd_miso),
		.absent(1'b0), .fail_read(1'b0), .fail_write(1'b0),
		.stuck_busy(1'b0), .bad_ocr(1'b0), .bad_echo(fail_boot),
		.bad_status(1'b0)
	);

	always @(posedge clk) begin
		if (trace_rom && !reset && ready && request && guest_bus.program_selected)
			$display("AM4 ROM fetch: pc=%06o address=%06o word=%06o phase=%0d",
				engine.alu.q_ram[7], address, read_data, guest_bus.boot_rom_phase);
		if (!reset) clocks <= clocks + 1;
		if (!reset && !fail_boot && !boot_complete && request &&
			address == 16'o177560)
			$fatal(1, "AM4 bootstrap entered ODT: pc=%06o sp=%06o commands=%0d reads=%0d power=%0d",
				engine.alu.q_ram[7], engine.alu.q_ram[6], card.command_count,
				card.read_count, card.power_clocks);
		if (clocks >= 10000000)
			$fatal(1, "AM4 SD boot execute timeout: fail=%b complete=%b ma=%03h ir=%06o address=%06o pc=%06o sp=%06o commands=%0d reads=%0d power=%0d",
				fail_boot, boot_complete, engine.ma, engine.ireg, address,
				engine.alu.q_ram[7], engine.alu.q_ram[6], card.command_count,
				card.read_count, card.power_clocks);
	end

	initial begin
		fail_boot = $test$plusargs("FAIL_BOOT");
		// Reproduce the physical card's observed FF,3F,01 CMD0 response.
		card.cmd0_bad_prefix = 1;
		// The hardware-proven transport brackets every command with CS high.
		// Catch regressions back to an indefinitely selected command stream.
		card.require_command_cs_toggle = 1;
		for (i = 0; i < 1024; i = i + 1) card.memory[i] = 0;
		#1 $readmemh("build/am4_sd_boot.hex", card.memory);
		repeat (15) @(negedge clk);
		reset = 0;
		if (fail_boot) begin
			wait (request && address == 16'o177560);
			if (boot_complete || !guest_bus.boot_overlay_active)
				$fatal(1, "AM4 failed SD boot removed retained ODT overlay");
			if (card.read_count != 0 || card.write_commands != 0)
				$fatal(1, "AM4 failed SD init reached media transfer");
			$display("PASS: AM4 SD bootstrap failure retained ODT (%0d clocks)", clocks);
		end else begin
			while ({fram.memory[17'o003001], fram.memory[17'o003000]} != 16'o012345 &&
				{fram.memory[17'o003001], fram.memory[17'o003000]} != 16'o065432)
				repeat (100) @(negedge clk);
			if ({fram.memory[17'o003001], fram.memory[17'o003000]} !== 16'o012345)
				$fatal(1, "AM4 SD bootstrap ABI/overlay failed: marker=%06o",
					{fram.memory[17'o003001], fram.memory[17'o003000]});
			if (!boot_complete || guest_bus.boot_overlay_active)
				$fatal(1, "AM4 successful SD boot did not remove overlay safely");
			if (card.read_count != 2 || card.write_commands != 0)
				$fatal(1, "AM4 SD boot command count mismatch");
			if (engine.cc6)
				$fatal(1, "AM4 SD-booted guest raised Q-bus timeout");
			$display("PASS: spare-MicROM PDP-11 bootstrap loaded SD LBA0/1 into FRAM and preserved ABI (%0d clocks)",
				clocks);
		end
		$finish;
	end
endmodule
