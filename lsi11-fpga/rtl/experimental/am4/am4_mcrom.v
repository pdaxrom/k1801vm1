//
// Copyright (c) 2020 by 1801BM1@gmail.com
//
//______________________________________________________________________________
//
// M4 microcode ROM, for debug and simulating only
//
module am4_mcrom #(
   parameter MICROM_FILE = "am4_mc.rom",
   parameter BOOTROM_FILE = ""
)
(
   input       clk,     // input clock
   input       ena,     // clock enable
   input [9:0] addr,    // instruction address
   output [55:0] data,  // output read opcode
   input          boot_ena,
   input [9:0]    boot_addr,
   output [7:0]   boot_data
);

//______________________________________________________________________________
//
// Memory array and its inititialization with K1656RE1-001/007 content
//
reg [55:0] rom [0:1023];
reg [55:0] q;
reg [7:0] boot_rom [0:1023];
reg [7:0] boot_q;
integer i;

initial
begin
end


initial
begin
   for (i=0; i<1024; i = i + 1)
   begin
      rom[i] = 56'h00000000000000;
      boot_rom[i] = 8'h00;
   end
   //
   // The filename for MicROM content might be explicitly
   // specified in synthesys/simulating tool settings
   //
   $readmemh(MICROM_FILE, rom);
   if (BOOTROM_FILE != "")
      $readmemh(BOOTROM_FILE, boot_rom);
end

//______________________________________________________________________________
//
assign data = q;
assign boot_data = boot_q;
always @ (posedge clk) if (ena) q <= rom[addr];
always @ (posedge clk) if (boot_ena) boot_q <= boot_rom[boot_addr];

endmodule
