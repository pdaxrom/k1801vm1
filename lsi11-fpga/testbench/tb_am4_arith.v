`timescale 1ns/1ps

// Direct-memory execution harness shared by assembled EIS and FIS tests.
module tb_am4_arith;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	localparam [15:0] DONE_ADDR = 16'o006000;
	reg clk = 0, reset = 1, ready = 0, request_seen = 0;
	wire peripheral_reset;
	wire [15:0] address, write_data, read_data;
	wire request, write_enable, vector_request;
	wire [1:0] byte_select;
	reg [7:0] memory [0:65535];
	reg [1023:0] program_file;
	integer i, cycles, max_cycles = 2000000;
	always #5 clk = !clk;
	assign read_data = {memory[{address[15:1], 1'b0}+1], memory[{address[15:1], 1'b0}]};
	am4_direct #(.MICROM_FILE(MICROM_FILE)) dut (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0), .event_request(1'b0), .vector_irq(1'b0),
		.bus_address(address), .bus_write_data(write_data), .bus_read_data(read_data),
		.bus_request(request), .bus_write(write_enable), .bus_byte_select(byte_select),
		.bus_instruction_fetch(),
		.bus_ready(ready), .vector_data(16'b0), .vector_ready(1'b0),
		.vector_request(vector_request), .boot_rom_ena(1'b0),
		.boot_rom_addr(10'b0), .boot_rom_data(), .boot_select(2'b11));
	always @(posedge clk) begin
		ready <= 0;
		if (reset || !request) request_seen <= 0;
		else if (!request_seen) begin request_seen <= 1; ready <= 1; end
		if (!reset && request && ready && write_enable) begin
			if (byte_select[0]) memory[address] <= write_data[7:0];
			if (byte_select[1]) memory[{address[15:1],1'b0}+1] <= write_data[15:8];
		end
		if (request && vector_request) $fatal(1, "overlapping requests");
	end
	task dump_state;
		begin
			$display("PROGRAM=%0s DONE=%06o CASE=%06o AUX=%06o %06o %06o %06o cycles=%0d ma=%03h ir=%06o psw=%03o r0=%06o r1=%06o r2=%06o r3=%06o r4=%06o sp=%06o",
				program_file, {memory[DONE_ADDR+1],memory[DONE_ADDR]},
				{memory[16'o6003],memory[16'o6002]},
				{memory[16'o6005],memory[16'o6004]}, {memory[16'o6007],memory[16'o6006]},
				{memory[16'o6011],memory[16'o6010]}, {memory[16'o6013],memory[16'o6012]}, cycles,
				dut.ma,dut.ireg,dut.psw,dut.alu.q_ram[0],dut.alu.q_ram[1],dut.alu.q_ram[2],
				dut.alu.q_ram[3],dut.alu.q_ram[4],dut.alu.q_ram[6]);
			$display("OPERANDS=%06o %06o %06o %06o STACK=%06o %06o %06o",
				{memory[16'o4001],memory[16'o4000]}, {memory[16'o4003],memory[16'o4002]},
				{memory[16'o4005],memory[16'o4004]}, {memory[16'o4007],memory[16'o4006]},
				{memory[16'o7773],memory[16'o7772]}, {memory[16'o7775],memory[16'o7774]},
				{memory[16'o7777],memory[16'o7776]});
		end
	endtask
	initial begin
		if (!$value$plusargs("PROGRAM=%s", program_file)) $fatal(1, "missing PROGRAM");
		if ($value$plusargs("TIMEOUT=%d", max_cycles)) begin end
		for (i=0;i<65536;i=i+1) memory[i]=0;
		$readmemh(program_file,memory);
		repeat (15) @(negedge clk); reset=0;
		for (cycles=0; cycles<max_cycles && {memory[DONE_ADDR+1],memory[DONE_ADDR]}==0; cycles=cycles+1) @(negedge clk);
		if (cycles>=max_cycles) begin dump_state; $fatal(1,"AM4 arithmetic timeout"); end
		if ({memory[DONE_ADDR+1],memory[DONE_ADDR]}!==16'o1) begin
			dump_state;
			$fatal(1,"AM4 arithmetic guest failure");
		end
		$display("PASS: AM4 arithmetic program %0s (%0d clocks)", program_file, cycles);
		$finish;
	end
endmodule
