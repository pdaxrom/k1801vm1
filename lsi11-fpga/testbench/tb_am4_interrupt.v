`timescale 1ns/1ps

// Exercise the direct vector handshake through an actual AM4 interrupt entry,
// stack save, handler and RTI.  Both data and vector responders are registered.
module tb_am4_interrupt;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire [15:0] address, write_data;
	wire [15:0] read_data;
	wire request, write_enable;
	wire [1:0] byte_select;
	reg bus_ready = 0;
	reg bus_pending = 0;
	reg vector_irq = 0;
	wire vector_request;
	reg vector_ready = 0;
	reg vector_pending = 0;
	reg [1:0] vector_wait = 0;
	reg program_ready = 0;
	reg [7:0] memory [0:65535];
	integer index;
	integer clocks = 0;
	integer vector_accepts = 0;

	always #5 clk = !clk;
	assign read_data = {memory[{address[15:1], 1'b0} + 1],
		memory[{address[15:1], 1'b0}]};

	am4_direct #(.MICROM_FILE(MICROM_FILE)) dut (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(1'b0), .vector_irq(vector_irq),
		.bus_address(address), .bus_write_data(write_data),
		.bus_read_data(read_data), .bus_request(request),
		.bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_instruction_fetch(),
		.bus_ready(bus_ready), .vector_data(16'o000060),
		.vector_ready(vector_ready), .vector_request(vector_request),
		.boot_rom_ena(1'b0), .boot_rom_addr(10'b0), .boot_rom_data(),
		.boot_select(2'b11)
	);

	always @(posedge clk) begin
		if (reset) begin
			bus_ready <= 0;
			bus_pending <= 0;
		end else begin
			bus_ready <= 0;
			if (bus_pending) begin
				bus_ready <= 1;
				bus_pending <= 0;
				if (write_enable) begin
					if (byte_select[0]) memory[address] <= write_data[7:0];
					if (byte_select[1])
						memory[{address[15:1], 1'b0} + 1] <= write_data[15:8];
					if ({address[15:1], 1'b0} == 16'o000200)
						program_ready <= 1;
				end
			end else if (request && !bus_ready)
				bus_pending <= 1;

			if (request && vector_request)
				$fatal(1, "ordinary and vector requests overlap during interrupt");
		end
	end

	always @(posedge clk) begin
		if (reset) begin
			vector_ready <= 0;
			vector_pending <= 0;
			vector_wait <= 0;
			vector_accepts <= 0;
		end else begin
			vector_ready <= 0;
			if (vector_pending) begin
				if (!vector_request)
					$fatal(1, "vector request dropped before ready");
				if (vector_wait != 0)
					vector_wait <= vector_wait - 1'b1;
				else begin
					vector_ready <= 1;
					vector_pending <= 0;
					vector_irq <= 0;
					vector_accepts <= vector_accepts + 1;
				end
			end else if (vector_request && !vector_ready) begin
				vector_pending <= 1;
				vector_wait <= 2;
			end
		end
	end

	always @(posedge clk) begin
		if (!reset) clocks <= clocks + 1;
		if (clocks >= 10000)
			$fatal(1, "AM4 vector timeout: ma=%03h ir=%06o address=%06o accepts=%0d",
				dut.ma, dut.ireg, address, vector_accepts);
	end

	initial begin
		for (index = 0; index < 65536; index = index + 1)
			memory[index] = 0;
		$readmemh("build/am4_interrupt.hex", memory);
		repeat (15) @(negedge clk);
		reset = 0;
		wait (program_ready);
		repeat (20) @(negedge clk);
		vector_irq = 1;
		wait ({memory[16'o201], memory[16'o200]} == 16'o000001);
		repeat (100) @(negedge clk);
		if (vector_accepts != 1)
			$fatal(1, "AM4 accepted %0d vectors, expected one", vector_accepts);
		if ({memory[16'o201], memory[16'o200]} !== 16'o000001)
			$fatal(1, "AM4 interrupt handler result changed: %06o",
				{memory[16'o201], memory[16'o200]});
		$display("PASS: direct AM4 accepted delayed vector 060, ran handler and returned with RTI (%0d clocks)",
			clocks);
		$finish;
	end
endmodule
