`timescale 1ns/1ps

// Execute the same MOV/CMP/BNE program as the preserved upstream-Wishbone
// test, but add deterministic 0..3-clock wait states to every direct cycle.
// The assertions check the request/ready contract required by FRAM and I/O.
module tb_am4_direct;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire [15:0] address, write_data;
	wire [15:0] read_data;
	wire request, write_enable, vector_request;
	wire [1:0] byte_select;
	reg ready = 0;
	reg [7:0] memory [0:65535];
	reg active = 0;
	reg [1:0] wait_count = 0;
	reg [15:0] held_address = 0;
	reg [15:0] held_write_data = 0;
	reg held_write = 0;
	reg [1:0] held_byte_select = 0;
	reg [3:0] wait_seen = 0;
	integer index;
	integer clocks = 0;
	integer reads = 0;
	integer writes = 0;
	integer transactions = 0;

	always #5 clk = !clk;
	assign read_data = {memory[{address[15:1], 1'b0} + 1],
		memory[{address[15:1], 1'b0}]};

	am4_direct #(.MICROM_FILE(MICROM_FILE)) dut (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(1'b0), .vector_irq(1'b0),
		.bus_address(address), .bus_write_data(write_data),
		.bus_read_data(read_data), .bus_request(request),
		.bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_instruction_fetch(),
		.bus_ready(ready), .vector_data(16'b0), .vector_ready(1'b0),
		.vector_request(vector_request), .boot_rom_ena(1'b0),
		.boot_rom_addr(10'b0), .boot_rom_data(), .boot_select(2'b11)
	);

	always @(posedge clk) begin
		if (reset) begin
			ready <= 0;
			active <= 0;
			wait_count <= 0;
		end else begin
			ready <= 0;
			if (active) begin
				if (!request)
					$fatal(1, "direct request dropped before ready");
				if (address !== held_address || write_data !== held_write_data ||
					write_enable !== held_write || byte_select !== held_byte_select)
					$fatal(1, "direct bus controls changed while waiting");
				if (wait_count != 0)
					wait_count <= wait_count - 1'b1;
				else begin
					ready <= 1;
					active <= 0;
				end
			end else if (request && !ready) begin
				held_address <= address;
				held_write_data <= write_data;
				held_write <= write_enable;
				held_byte_select <= byte_select;
				wait_seen[transactions[1:0]] <= 1'b1;
				if (transactions[1:0] == 0) begin
					ready <= 1;
					active <= 0;
				end else begin
					active <= 1;
					wait_count <= transactions[1:0] - 1'b1;
				end
				transactions <= transactions + 1;
			end

			if (request && ready) begin
				if (write_enable) begin
					if (byte_select[0]) memory[address] <= write_data[7:0];
					if (byte_select[1])
						memory[{address[15:1], 1'b0} + 1] <= write_data[15:8];
					writes <= writes + 1;
				end else
					reads <= reads + 1;
			end

			if (request && vector_request)
				$fatal(1, "ordinary and vector requests overlap");
		end
	end

	initial begin
		for (index = 0; index < 65536; index = index + 1)
			memory[index] = 0;
		$readmemh("build/lsi11_original_smoke.hex", memory);
		repeat (15) @(negedge clk);
		reset = 0;
		while ({memory[16'o203], memory[16'o202]} == 0 && clocks < 25000) begin
			@(negedge clk);
			clocks = clocks + 1;
		end
		if (clocks >= 25000)
			$fatal(1, "direct AM4 timeout: ma=%03h ir=%06o adr=%06o reads=%0d writes=%0d",
				dut.ma, dut.ireg, address, reads, writes);
		if ({memory[16'o201], memory[16'o200]} !== 16'o012345)
			$fatal(1, "direct AM4 MOV mismatch: %06o",
				{memory[16'o201], memory[16'o200]});
		if ({memory[16'o203], memory[16'o202]} !== 16'o000001)
			$fatal(1, "direct AM4 branch mismatch: %06o",
				{memory[16'o203], memory[16'o202]});
		if (vector_request)
			$fatal(1, "unexpected interrupt acknowledge");
		if (wait_seen !== 4'b1111)
			$fatal(1, "not all direct-bus wait depths were exercised: %b",
				wait_seen);
		$display("PASS: direct AM4 executed MOV/CMP/BNE with 0..3 wait states (%0d clocks, %0d reads, %0d writes)",
			clocks, reads, writes);
		$finish;
	end
endmodule
