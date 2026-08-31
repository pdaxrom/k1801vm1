`timescale 1ns/1ps

// Boot through the spare-bit PDP-11 loader, then issue RK READ and WRITE.  The
// private vector executes the service from the remaining MicROM spare bits,
// returns through RTI, and delivers guest completion vector 0210.
module tb_am4_rk_service;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire request, write_enable, ready;
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
	integer traced_sd_ops = 0;
	reg trace_rk = 0;
	reg trace_rom = 0;

	always #5 clk = !clk;
	initial trace_rk = $test$plusargs("TRACE_RK");
	initial trace_rom = $test$plusargs("TRACE_ROM");

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(200), .SD_BOOT_ENABLE(1),
		.RK_SERVICE_ENABLE(1), .SD_SLOW_DIV(3), .SD_FAST_DIV(1)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(request), .write(write_enable), .byte_select(byte_select),
		.address(address), .wdata(write_data), .rdata(read_data),
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
		.stuck_busy(1'b0), .bad_ocr(1'b0), .bad_echo(1'b0),
		.bad_status(1'b0)
	);

	always @(posedge clk) begin
		if (!reset) clocks <= clocks + 1;
		if (trace_rom && !reset && ready && request &&
			guest_bus.program_selected &&
			((address >= 16'o004400 && address < 16'o004700) ||
			 address >= 16'o160000))
			$display("AM4 ROM fetch: pc=%06o address=%06o word=%06o phase=%0d",
				engine.alu.q_ram[7], address, read_data, guest_bus.boot_rom_phase);
		if (trace_rk && !reset && card.write_commands != 0 && ready && request &&
			{address[15:1], 1'b0} == 16'o177500 && traced_sd_ops < 24) begin
			$display("AM4 SD op %0d: pc=%06o write=%b data=%03o response=%03o count=%0d busy=%0d writes=%0d",
				traced_sd_ops, engine.alu.q_ram[7], write_enable,
				write_data[7:0], read_data[7:0], card.write_count,
				card.busy_bytes, card.writes);
			traced_sd_ops <= traced_sd_ops + 1;
		end
		if (trace_rk && clocks != 0 && clocks % 500000 == 0)
			$display("AM4 RK progress: clocks=%0d pc=%06o wc=%06o ba=%06o sd_count=%0d busy=%0d writes=%0d",
				clocks, engine.alu.q_ram[7],
				{fram.memory[18'o200003], fram.memory[18'o200002]},
				{fram.memory[18'o200005], fram.memory[18'o200004]},
				card.write_count, card.busy_bytes, card.writes);
		if (!reset && request && address == 16'o177560)
			$fatal(1, "AM4 RK service entered ODT: pc=%06o ir=%06o sp=%06o cs1=%06o wc=%06o ba=%06o service=%b release=%b irq=%b marker=%06o",
				engine.alu.q_ram[7], engine.ireg, engine.alu.q_ram[6],
				{fram.memory[18'o200001], fram.memory[18'o200000]},
				{fram.memory[18'o200003], fram.memory[18'o200002]},
				{fram.memory[18'o200005], fram.memory[18'o200004]},
				guest_bus.rk_service_active, guest_bus.rk_service_release,
				guest_bus.rk_irq_pending,
				{fram.memory[17'o003201], fram.memory[17'o003200]});
		if (!reset && guest_bus.service_program_selected &&
			!guest_bus.rk_service_active)
			$fatal(1, "private service ROM escaped its active window");
		if (clocks >= 30000000)
			$fatal(1, "AM4 RK service timeout: complete=%b service=%b pending=%b pc=%06o ir=%06o cs1=%06o wc=%06o ba=%06o commands=%0d reads=%0d",
				boot_complete, guest_bus.rk_service_active,
				guest_bus.rk_service_pending, engine.alu.q_ram[7],
				engine.ireg,
				{fram.memory[18'o200001], fram.memory[18'o200000]},
				{fram.memory[18'o200003], fram.memory[18'o200002]},
				{fram.memory[18'o200005], fram.memory[18'o200004]},
				card.command_count, card.read_count);
	end

	initial begin
		card.require_command_cs_toggle = 1;
		for (i = 0; i < 131072; i = i + 1) begin
			fram.memory[i] = 0;
			card.memory[i] = 0;
		end
		$readmemh("build/am4_rk_read.hex", card.memory);
		card.memory[21*512+0] = 8'h5a;
		card.memory[21*512+1] = 8'hb0;
		card.memory[21*512+2] = 8'h5b;
		card.memory[21*512+3] = 8'hb0;
		card.memory[21*512+4] = 8'h58;
		card.memory[21*512+5] = 8'hb0;
		repeat (15) @(negedge clk);
		reset = 0;
		while ({fram.memory[17'o003201], fram.memory[17'o003200]} != 16'o012345 &&
			{fram.memory[17'o003201], fram.memory[17'o003200]} != 16'o065432)
			repeat (100) @(negedge clk);
		if ({fram.memory[17'o003201], fram.memory[17'o003200]} !== 16'o012345)
			$fatal(1, "AM4 RK guest failed: marker=%06o pc=%06o ir=%06o cs1=%06o wc=%06o ba=%06o da=%06o dc=%06o data=%06o/%06o/%06o er1=%06o service=%b pending=%b irq=%b commands=%0d reads=%0d",
				{fram.memory[17'o003201], fram.memory[17'o003200]},
				engine.alu.q_ram[7], engine.ireg,
				{fram.memory[18'o200001], fram.memory[18'o200000]},
				{fram.memory[18'o200003], fram.memory[18'o200002]},
				{fram.memory[18'o200005], fram.memory[18'o200004]},
				{fram.memory[18'o200007], fram.memory[18'o200006]},
				{fram.memory[18'o200021], fram.memory[18'o200020]},
				{fram.memory[17'o003001], fram.memory[17'o003000]},
				{fram.memory[17'o003003], fram.memory[17'o003002]},
				{fram.memory[17'o003005], fram.memory[17'o003004]},
				{fram.memory[18'o200015], fram.memory[18'o200014]},
				guest_bus.rk_service_active, guest_bus.rk_service_pending,
				guest_bus.rk_irq_pending, card.command_count, card.read_count);
		if (!boot_complete || guest_bus.boot_overlay_active ||
			guest_bus.rk_service_active || guest_bus.rk_service_pending)
			$fatal(1, "AM4 RK service did not restore guest execution");
		if (card.read_count != 3 || card.write_commands != 1 || card.writes != 1)
			$fatal(1, "AM4 RK command count mismatch: reads=%0d writes=%0d",
				card.read_count, card.write_commands);
		for (i = 0; i < 256; i = i + 1)
			if ({card.memory[7*512+i*2+1], card.memory[7*512+i*2]} !==
				(16'o012345 + i[15:0]))
				$fatal(1, "AM4 RK WRITE data mismatch at word %0d: %06o",
					i, {card.memory[7*512+i*2+1], card.memory[7*512+i*2]});
		if ({fram.memory[17'o014001], fram.memory[17'o014000]} !== 16'o000002)
			$fatal(1, "AM4 RK completion vector was not delivered twice");
		if (engine.cc6)
			$fatal(1, "AM4 RK service raised Q-bus timeout");
		$display("PASS: AM4 private MicROM service completed RK READ/WRITE and vector 0210 (%0d clocks)",
			clocks);
		$finish;
	end
endmodule
