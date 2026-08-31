`timescale 1ns/1ps

// Check the architectural bus contract for MOVB memory,memory.  Pointer and
// index-word reads are allowed; the final destination operand is never read.
module tb_am4_movb_bus;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	localparam [15:0] SOURCE_ADDR = 16'o000600;
	localparam [15:0] RESULT_BASE = 16'o000620;
	localparam [15:0] DONE_ADDR = 16'o000640;
	localparam [15:0] IO_BASE = 16'o177000;
	localparam integer TARGETS = 9;

	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire [15:0] address, write_data;
	wire [15:0] read_data;
	wire request, write_enable, vector_request;
	wire [1:0] byte_select;
	reg ready = 0;
	reg request_seen = 0;
	reg [7:0] memory [0:65535];
	integer destination_reads [0:TARGETS-1];
	integer destination_writes [0:TARGETS-1];
	integer source_reads = 0;
	integer clocks = 0;
	integer index;

	always #5 clk = !clk;
	always @(posedge clk)
		if ($test$plusargs("TRACE_MA") && clocks < 2000)
			$display("MICRO ma=%03h ir=%06o r0=%06o r10=%06o areg=%06o mcr=%014h",
				dut.ma, dut.ireg, dut.alu.q_ram[0], dut.alu.q_ram[10],
				dut.areg, dut.mcr);
	assign read_data = {memory[{address[15:1], 1'b0} + 1],
		memory[{address[15:1], 1'b0}]};

	am4_direct #(.MICROM_FILE(MICROM_FILE)) dut (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(1'b0), .vector_irq(1'b0),
		.bus_address(address), .bus_write_data(write_data),
		.bus_read_data(read_data), .bus_request(request),
		.bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_ready(ready), .vector_data(16'b0), .vector_ready(1'b0),
		.vector_request(vector_request), .boot_rom_ena(1'b0),
		.boot_rom_addr(10'b0), .boot_rom_data(), .boot_select(2'b11)
	);

	function integer target_index;
		input [15:0] bus_address;
		begin
			case (bus_address)
				IO_BASE + 0:  target_index = 0;
				IO_BASE + 1:  target_index = 1;
				IO_BASE + 2:  target_index = 2;
				IO_BASE + 3:  target_index = 3;
				IO_BASE + 4:  target_index = 4;
				IO_BASE + 5:  target_index = 5;
				IO_BASE + 6:  target_index = 6;
				IO_BASE + 7:  target_index = 7;
				IO_BASE + 8:  target_index = 8;
				default:      target_index = -1;
			endcase
		end
	endfunction

	always @(posedge clk) begin : bus_cycle
		integer selected_target;
		ready <= 0;
		if (reset) begin
			request_seen <= 0;
		end else if (!request) begin
			request_seen <= 0;
		end else if (!request_seen) begin
			request_seen <= 1;
			ready <= 1;
		end

		if (!reset && request && ready) begin
			selected_target = target_index(address);
			if ($test$plusargs("TRACE"))
				$display("BUS ma=%03h ir=%06o %s adr=%06o sel=%b data=%06o",
					dut.ma, dut.ireg, write_enable ? "WR" : "RD",
					address, byte_select, write_enable ? write_data : read_data);
			if (write_enable) begin
				if (byte_select[0]) memory[address] <= write_data[7:0];
				if (byte_select[1])
					memory[{address[15:1], 1'b0} + 1] <= write_data[15:8];
				if (selected_target >= 0)
					destination_writes[selected_target] <=
						destination_writes[selected_target] + 1;
			end else begin
				if (address == SOURCE_ADDR)
					source_reads <= source_reads + 1;
				if (selected_target >= 0)
					destination_reads[selected_target] <=
						destination_reads[selected_target] + 1;
			end
		end

		if (request && vector_request)
			$fatal(1, "ordinary and vector requests overlap");
	end

	initial begin
		for (index = 0; index < 65536; index = index + 1)
			memory[index] = 0;
		for (index = 0; index < TARGETS; index = index + 1) begin
			destination_reads[index] = 0;
			destination_writes[index] = 0;
		end
		$readmemh("build/am4_movb_bus.hex", memory);
		repeat (15) @(negedge clk);
		reset = 0;
		while ({memory[DONE_ADDR + 1], memory[DONE_ADDR]} == 0 &&
			clocks < 50000) begin
			@(negedge clk);
			clocks = clocks + 1;
		end
		if (clocks >= 50000)
			$fatal(1, "AM4 MOVB timeout: ma=%03h ir=%06o adr=%06o",
				dut.ma, dut.ireg, address);

		for (index = 0; index < TARGETS; index = index + 1) begin
			if (destination_reads[index] != 0)
				$fatal(1, "destination %0d was read %0d times",
					index, destination_reads[index]);
			if (destination_writes[index] != 1)
				$fatal(1, "destination %0d was written %0d times",
					index, destination_writes[index]);
		end
		if (source_reads != TARGETS)
			$fatal(1, "source was read %0d times, expected %0d",
				source_reads, TARGETS);
		for (index = 0; index < TARGETS; index = index + 1)
			if (memory[IO_BASE + (index < 8 ? index : 8)] !== 8'o123)
				$fatal(1, "destination %0d data mismatch", index);

		if ({memory[RESULT_BASE + 1], memory[RESULT_BASE]} !== IO_BASE + 2)
			$fatal(1, "R0 byte autoincrement step mismatch");
		if ({memory[RESULT_BASE + 3], memory[RESULT_BASE + 2]} !== 16'o000502)
			$fatal(1, "deferred autoincrement step mismatch");
		if ({memory[RESULT_BASE + 5], memory[RESULT_BASE + 4]} !== IO_BASE + 3)
			$fatal(1, "R0 byte autodecrement step mismatch");
		if ({memory[RESULT_BASE + 7], memory[RESULT_BASE + 6]} !== 16'o000502)
			$fatal(1, "deferred autodecrement step mismatch");
		if ({memory[RESULT_BASE + 9], memory[RESULT_BASE + 8]} !== IO_BASE + 9)
			$fatal(1, "SP byte autoincrement step mismatch");
		if ({memory[RESULT_BASE + 11], memory[RESULT_BASE + 10]} !== IO_BASE + 8)
			$fatal(1, "SP byte autodecrement step mismatch");

		$display("PASS: AM4 MOVB used store-only destination cycles for modes 1..7 (%0d clocks)",
			clocks);
		$finish;
	end
endmodule
