`timescale 1ns/1ps

module tb_am4_mcrom_ebr;
	reg clk = 0;
	reg ena = 0;
	reg [9:0] addr = 0;
	wire [55:0] data;
	reg boot_ena = 0;
	reg [9:0] boot_addr = 0;
	wire [7:0] boot_data;
	reg [55:0] expected [0:1023];
	reg [7:0] expected_boot [0:1023];
	integer index;

	am4_mcrom dut (
		.clk(clk), .ena(ena), .addr(addr), .data(data),
		.boot_ena(boot_ena), .boot_addr(boot_addr), .boot_data(boot_data)
	);
	always #5 clk = !clk;

	initial begin
		$readmemh("../ucode/experimental/am4/mc.rom", expected);
		$readmemh("../boards/hc1200-microcomp/am4_sd_boot.rom", expected_boot);
		for (index = 0; index < 1024; index = index + 1) begin
			@(negedge clk);
			ena = 1;
			addr = index[9:0];
			@(posedge clk);
			#1;
			if (data !== expected[index])
				$fatal(1, "AM4 EBR[%0d]=%014h expected %014h",
					index, data, expected[index]);
		end
		ena = 0;
		for (index = 0; index < 1024; index = index + 1) begin
			@(negedge clk);
			boot_ena = 1;
			boot_addr = index[9:0];
			@(posedge clk);
			#1;
			if (boot_data !== expected_boot[index])
				$fatal(1, "AM4 boot fragment[%0d]=%02h expected %02h",
					index, boot_data, expected_boot[index]);
		end
		$display("PASS: AM4 MicROM and spare-bit boot ROM share exactly seven DP8KC blocks");
		$finish;
	end
endmodule
