`timescale 1ns/1ps

// Diagnostic/acceptance harness for the user-owned RT-11 image.  It boots
// through the actual AM4 spare-bit loader and reports every guest RK command.
module tb_am4_rt11;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	parameter ENABLE_TIMER = 1;
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
	integer rk_writes = 0;
	integer uart_bytes = 0;
	integer timer_edges = 0;
	reg previous_boot_complete = 0;
	reg previous_timer = 0;
	reg [39:0] uart_window = 0;
	reg [7:0] previous_uart = 0;
	reg saw_banner = 0;
	reg trace_rk = 0;

	always #5 clk = !clk;
	initial trace_rk = $test$plusargs("TRACE_RK");

	am4_hc1200_cpu11_bus #(
		.FRAM_CLK_DIV(2), .TICK_DIVISOR(532000), .SD_BOOT_ENABLE(1),
		.RK_SERVICE_ENABLE(1), .SD_SLOW_DIV(68), .SD_FAST_DIV(2)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(request), .write(write_enable), .byte_select(byte_select),
		.address(address), .wdata(write_data), .rdata(read_data),
		.acknowledge(ready), .virq(virq), .interrupt_vector(vector_data),
		.interrupt_strobe(vector_request),
		.interrupt_acknowledge(vector_ready), .event_irq(timer_event),
		.timer_enable(1'b1), .uart_rx(1'b1), .uart_tx(uart_tx),
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
		.event_request(ENABLE_TIMER && boot_complete ? timer_event : 1'b0),
		.vector_irq(virq),
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
		if (!reset && timer_event && !previous_timer)
			timer_edges <= timer_edges + 1;
		previous_timer <= timer_event;
		if (!previous_boot_complete && boot_complete)
			$display("AM4 RT11 handoff: clocks=%0d PC=%06o", clocks,
				engine.alu.q_ram[7]);
		previous_boot_complete <= boot_complete;
		if (!reset && ready && request && write_enable &&
			{address[15:1], 1'b0} == 16'o177440) begin
			rk_writes <= rk_writes + 1;
			if (trace_rk)
				$display("AM4 RKCS1 write %0d: clocks=%0d PC=%06o value=%06o WC=%06o BA=%06o DA=%06o DC=%06o",
					rk_writes + 1, clocks, engine.alu.q_ram[7], write_data,
					{fram.memory[17'o200003], fram.memory[17'o200002]},
					{fram.memory[17'o200005], fram.memory[17'o200004]},
					{fram.memory[17'o200007], fram.memory[17'o200006]},
					{fram.memory[17'o200021], fram.memory[17'o200020]});
		end
		if (!reset && ready && request && write_enable &&
			{address[15:1], 1'b0} == 16'o177566) begin
			uart_bytes <= uart_bytes + 1;
			uart_window <= {uart_window[31:0], write_data[7:0]};
			previous_uart <= write_data[7:0];
			$write("%c", write_data[7:0]);
			if ({uart_window[31:0], write_data[7:0]} == "RT-11")
				saw_banner <= 1;
			if (saw_banner && previous_uart == 8'h0a &&
				write_data[7:0] == ".") begin
				if (!guest_bus.timer_armed || timer_edges == 0)
					$fatal(1, "AM4 RT11 prompt arrived without an active 50 Hz timer");
				$display("\nPASS: AM4 RT-11 prompt after %0d RKCS1 writes and %0d timer edges (%0d clocks)",
					rk_writes, timer_edges, clocks);
				$finish;
			end
		end
		if (!reset && ready && request && write_enable &&
			({address[15:1], 1'b0} == 16'o000100 ||
			 {address[15:1], 1'b0} == 16'o000102) && trace_rk)
			$display("AM4 timer vector write: clocks=%0d address=%06o value=%06o",
				clocks, address, write_data);
		if (!reset && !boot_complete && request && address == 16'o177560)
			$fatal(1, "AM4 RT11 entered boot ODT: PC=%06o stage=%06o value=%06o",
				engine.alu.q_ram[7],
				{fram.memory[17'o157777], fram.memory[17'o157776]},
				{fram.memory[17'o157775], fram.memory[17'o157774]});
		if (trace_rk && clocks != 0 && clocks % 5000000 == 0)
			$display("AM4 RT11 progress: clocks=%0d PC=%06o IR=%06o commands=%0d reads=%0d service=%b pending=%b CS1=%06o",
				clocks, engine.alu.q_ram[7], engine.ireg, rk_writes,
				card.read_count, guest_bus.rk_service_active,
				guest_bus.rk_service_pending,
				{fram.memory[17'o200001], fram.memory[17'o200000]});
		if (clocks >= 300000000)
			$fatal(1, "AM4 RT11 timeout: PC=%06o IR=%06o commands=%0d reads=%0d CS1=%06o",
				engine.alu.q_ram[7], engine.ireg, rk_writes, card.read_count,
				{fram.memory[17'o200001], fram.memory[17'o200000]});
	end

	initial begin
		repeat (15) @(negedge clk);
		reset = 0;
	end
endmodule
