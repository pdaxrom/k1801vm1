`timescale 1ns/1ps

// Boot a real RT-11 image, run MACRO, and abort it with Ctrl-C.  The SD model
// keeps the backing image read-only and redirects every write to a RAM overlay.
// This reproduces the transfer that used to enter ODT and then write sectors
// forever because its RK word count was not a multiple of 256 words.
module tb_am4_ctrlc;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	reg uart_rx = 1;
	wire peripheral_reset;
	wire request, write_enable, ready, instruction_fetch;
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
	integer prompts = 0;
	integer rk_commands = 0;
	integer timer_edges = 0;
	integer serial_bit;
	reg previous_timer = 0;
	reg [39:0] uart_window = 0;
	reg [7:0] previous_uart = 0;
	reg saw_banner = 0;
	reg command_sent = 0;
	reg ctrl_c_sent = 0;
	reg saw_macro_prompt = 0;
	reg saw_partial_write = 0;
	reg partial_write_completed = 0;
	reg fast_busy_wait = 0;

	task automatic uart_send(input [7:0] data);
	begin
		uart_rx = 0;
		repeat (257) @(negedge clk);
		for (serial_bit = 0; serial_bit < 8; serial_bit = serial_bit + 1) begin
			uart_rx = data[serial_bit];
			repeat (257) @(negedge clk);
		end
		uart_rx = 1;
		repeat (257) @(negedge clk);
		// The KL11 has a one-byte receive register.  Give RT-11 time to consume
		// and echo this character before sending the next one.
		repeat (50000) @(negedge clk);
	end
	endtask

	always #5 clk = !clk;
	initial fast_busy_wait = $test$plusargs("FAST_BUSY_WAIT");

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(1), .TICK_DIVISOR(591200), .SD_BOOT_ENABLE(1),
		.RK_SERVICE_ENABLE(1), .SD_SLOW_DIV(68), .SD_FAST_DIV(2)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(request), .write(write_enable), .byte_select(byte_select),
		.address(address), .wdata(write_data),
		.instruction_fetch(instruction_fetch), .rdata(read_data),
		.acknowledge(ready), .virq(virq), .interrupt_vector(vector_data),
		.interrupt_strobe(vector_request),
		.interrupt_acknowledge(vector_ready), .event_irq(timer_event),
		.uart_rx(uart_rx), .uart_tx(uart_tx), .panel_key_rows(4'b0),
		.panel_din(), .panel_ce(), .panel_clk(), .panel_rs(),
		.panel_blank(), .panel_reg_latch(),
		.spi_cs_n(fram_cs_n), .spi_sck(fram_sck),
		.spi_mosi(fram_mosi), .spi_miso(fram_miso),
		.sd_cs_n(sd_cs_n), .sd_sck(sd_sck), .sd_mosi(sd_mosi),
		.sd_miso(sd_miso), .boot_rom_ena(boot_rom_ena),
		.boot_rom_addr(boot_rom_addr), .boot_rom_data(boot_rom_data),
		.boot_complete(boot_complete), .host_miso(), .host_miso_oe()
	);

	am4_direct #(
		.MICROM_FILE(MICROM_FILE), .BOOTROM_FILE("build/am4_bootrom.rom"),
		.BUS_TIMEOUT_BITS(11)
	) engine (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(timer_event), .vector_irq(virq),
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

	spi_sd_model #(.SECTORS(256)) card (
		.cs_n(sd_cs_n), .sck(sd_sck), .mosi(sd_mosi), .miso(sd_miso),
		.absent(1'b0), .fail_read(1'b0), .fail_write(1'b0),
		.stuck_busy(1'b0), .bad_ocr(1'b0), .bad_echo(1'b0),
		.bad_status(1'b0)
	);

	always @(posedge clk) begin
		if (!reset)
			clocks <= clocks + 1;
		if (!reset && timer_event && !previous_timer)
			timer_edges <= timer_edges + 1;
		previous_timer <= timer_event;

		// Optional simulation-only shortening of the bounded SD programming
		// delay.  It changes only the SOB iteration count, not the I/O path.
		if (fast_busy_wait && saw_macro_prompt && engine.io_rdy &&
			engine.ir_stb && engine.alu.q_ram[7] == 16'o160414)
			engine.alu.q_ram[1] = 1;

		if (ctrl_c_sent && engine.io_rdy && engine.ir_stb &&
			engine.alu_d == 16'o000000)
			$fatal(1, "AM4 fetched HALT after Ctrl-C: pc=%06o sp=%06o",
				engine.alu.q_ram[7], engine.alu.q_ram[6]);

		if (!reset && ready && request && write_enable &&
			{address[15:1], 1'b0} == 16'o177440) begin
			rk_commands <= rk_commands + 1;
			if (write_data[0] && write_data[5:2] == 4'o4 &&
				{fram.memory[17'o200003], fram.memory[17'o200002]} == 16'o163622)
				saw_partial_write <= 1;
		end

		// At the service RTI fetch, the formerly endless WRITE must have stopped
		// at zero and advanced BA only for the words supplied by RT-11.
		if (saw_partial_write && !partial_write_completed &&
			guest_bus.rk_service_active && guest_bus.boot_program_ack && request &&
			guest_bus.word_address == 16'o160476) begin
			if ({fram.memory[17'o200003], fram.memory[17'o200002]} != 0 ||
				{fram.memory[17'o200005], fram.memory[17'o200004]} != 16'o110334)
				$fatal(1, "partial WRITE completion mismatch: wc=%06o ba=%06o writes=%0d",
					{fram.memory[17'o200003], fram.memory[17'o200002]},
					{fram.memory[17'o200005], fram.memory[17'o200004]}, card.writes);
			partial_write_completed <= 1;
		end

		if (!reset && ready && request && write_enable &&
			{address[15:1], 1'b0} == 16'o177566) begin
			uart_window <= {uart_window[31:0], write_data[7:0]};
			previous_uart <= write_data[7:0];
			$write("%c", write_data[7:0]);
			if (command_sent && write_data[7:0] == "*")
				saw_macro_prompt <= 1;
			if ({uart_window[31:0], write_data[7:0]} == "RT-11")
				saw_banner <= 1;
			if (ctrl_c_sent && write_data[7:0] == "@")
				$fatal(1, "AM4 entered ODT after Ctrl-C: pc=%06o ir=%06o",
					engine.alu.q_ram[7], engine.ireg);
			if (saw_banner && previous_uart == 8'h0a && write_data[7:0] == ".") begin
				if (prompts == 0 && (!guest_bus.timer_ie || timer_edges == 0))
					$fatal(1, "RT-11 prompt arrived without an active 50 Hz timer");
				prompts <= prompts + 1;
				if (ctrl_c_sent && prompts == 3) begin
					if (!saw_partial_write || !partial_write_completed)
						$fatal(1, "Ctrl-C returned without completing the partial WRITE check");
					$display("\nPASS: RT-11 MACRO Ctrl-C returned to KMON; partial WRITE terminated (%0d clocks, %0d sectors written)",
						clocks, card.writes);
					$finish;
				end
			end
		end

		if (!reset && !boot_complete && request && address == 16'o177560)
			$fatal(1, "AM4 entered boot ODT: pc=%06o", engine.alu.q_ram[7]);
		if (clocks >= 900000000)
			$fatal(1, "AM4 Ctrl-C timeout: pc=%06o ir=%06o prompts=%0d writes=%0d",
				engine.alu.q_ram[7], engine.ireg, prompts, card.writes);
	end

	initial begin
		repeat (15) @(negedge clk);
		reset = 0;
		// This image's STARTX.COM emits two commands.  Wait for its third prompt
		// so input cannot interleave with the start-up command file.
		wait (prompts == 3);
		repeat (100000) @(negedge clk);
		command_sent = 1;
		uart_send("R"); uart_send(" "); uart_send("M"); uart_send("A");
		uart_send("C"); uart_send("R"); uart_send("O"); uart_send(8'h0d);
		wait (saw_macro_prompt);
		repeat (100000) @(negedge clk);
		if (prompts != 3)
			$fatal(1, "MACRO returned before Ctrl-C (prompts=%0d)", prompts);
		ctrl_c_sent = 1;
		uart_send(8'h03);
	end
endmodule
