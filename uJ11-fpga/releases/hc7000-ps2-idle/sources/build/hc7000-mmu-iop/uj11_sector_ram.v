// Generated 512-byte sector RAM; one 512x18 EBR.
module uj11_sector_ram(input wire clk, write, input wire [7:0] address,
 input wire [15:0] write_data,output wire [15:0] data);
wire enable=1'b1;wire [1:0] we={2{write}};wire [8:0] a={1'b0,address};
`ifdef SYNTHESIS
`define UJ11_IOP_EBR
`elsif UJ11_IOP_VENDOR_RAM
`define UJ11_IOP_EBR
`endif
`ifdef UJ11_IOP_EBR
PDPW8KC #(.DATA_WIDTH_W(18),.DATA_WIDTH_R(18),.REGMODE("NOREG"),
 .CSDECODE_W("0b000"),.CSDECODE_R("0b000"),
 .GSR("DISABLED"),.RESETMODE("SYNC"),.ASYNC_RESET_RELEASE("SYNC"),.INIT_DATA("STATIC")
) ram (
 .DI0(write_data[0]),
 .DO0(data[8]),
 .DI1(write_data[1]),
 .DO1(data[9]),
 .DI2(write_data[2]),
 .DO2(data[10]),
 .DI3(write_data[3]),
 .DO3(data[11]),
 .DI4(write_data[4]),
 .DO4(data[12]),
 .DI5(write_data[5]),
 .DO5(data[13]),
 .DI6(write_data[6]),
 .DO6(data[14]),
 .DI7(write_data[7]),
 .DO7(data[15]),
 .DI8(1'b0),
 .DO8(),
 .DI9(write_data[8]),
 .DO9(data[0]),
 .DI10(write_data[9]),
 .DO10(data[1]),
 .DI11(write_data[10]),
 .DO11(data[2]),
 .DI12(write_data[11]),
 .DO12(data[3]),
 .DI13(write_data[12]),
 .DO13(data[4]),
 .DI14(write_data[13]),
 .DO14(data[5]),
 .DI15(write_data[14]),
 .DO15(data[6]),
 .DI16(write_data[15]),
 .DO16(data[7]),
 .DI17(1'b0),
 .DO17(),
 .ADW0(a[0]),
 .ADW1(a[1]),
 .ADW2(a[2]),
 .ADW3(a[3]),
 .ADW4(a[4]),
 .ADW5(a[5]),
 .ADW6(a[6]),
 .ADW7(a[7]),
 .ADW8(a[8]),
 .ADR0(1'b0),
 .ADR1(1'b0),
 .ADR2(1'b0),
 .ADR3(1'b0),
 .ADR4(a[0]),
 .ADR5(a[1]),
 .ADR6(a[2]),
 .ADR7(a[3]),
 .ADR8(a[4]),
 .ADR9(a[5]),
 .ADR10(a[6]),
 .ADR11(a[7]),
 .ADR12(a[8]),
 .BE0(we[0]),
 .BE1(we[1]),
 .CEW(enable && (|we)),
 .CER(enable && !(|we)),
 .OCER(1'b1),
 .CLKW(clk),
 .CLKR(clk),
 .RST(1'b0),
 .CSW0(1'b0),
 .CSW1(1'b0),
 .CSW2(1'b0),
 .CSR0(1'b0),
 .CSR1(1'b0),
 .CSR2(1'b0)
);
`else
reg [15:0] words[0:255];reg [15:0] value;
always @(posedge clk) if(write)words[address]<=write_data;else value<=words[address];
assign data=value;
`endif
`undef UJ11_IOP_EBR
endmodule
