`timescale 1ns/1ps
// One MachXO2 1024x9 EBR, eight payload bits. Two line buffers and palette
// share the block. ADA0 is the x9 byte-write enable (not an address bit).
// No simultaneous read/write to one address is permitted.
module uj11_pal_ram(
    input wire write_clk, write_enable,
    input wire [9:0] write_address,
    input wire [7:0] write_data,
    input wire read_clk, read_enable,
    input wire [9:0] read_address,
    output wire [7:0] read_data
);
`ifdef SYNTHESIS
`define UJ11_PAL_EBR
`elsif UJ11_VIDEO_VENDOR_RAM
`define UJ11_PAL_EBR
`endif
`ifdef UJ11_PAL_EBR
    DP8KC #(.DATA_WIDTH_A(9),.DATA_WIDTH_B(9),.REGMODE_A("NOREG"),.REGMODE_B("NOREG"),
        .CSDECODE_A("0b000"),.CSDECODE_B("0b000"),.WRITEMODE_A("NORMAL"),.WRITEMODE_B("NORMAL"),
        .GSR("DISABLED"),.RESETMODE("SYNC"),.ASYNC_RESET_RELEASE("SYNC")) ebr(
        .DIA0(write_data[0]),
        .DIB0(1'b0),
        .DOA0(),
        .DOB0(read_data[0]),
        .DIA1(write_data[1]),
        .DIB1(1'b0),
        .DOA1(),
        .DOB1(read_data[1]),
        .DIA2(write_data[2]),
        .DIB2(1'b0),
        .DOA2(),
        .DOB2(read_data[2]),
        .DIA3(write_data[3]),
        .DIB3(1'b0),
        .DOA3(),
        .DOB3(read_data[3]),
        .DIA4(write_data[4]),
        .DIB4(1'b0),
        .DOA4(),
        .DOB4(read_data[4]),
        .DIA5(write_data[5]),
        .DIB5(1'b0),
        .DOA5(),
        .DOB5(read_data[5]),
        .DIA6(write_data[6]),
        .DIB6(1'b0),
        .DOA6(),
        .DOB6(read_data[6]),
        .DIA7(write_data[7]),
        .DIB7(1'b0),
        .DOA7(),
        .DOB7(read_data[7]),
        .DIA8(1'b0),
        .DIB8(1'b0),
        .DOA8(),
        .DOB8(),
        .ADA0(1'b1),
        .ADB0(1'b0),
        .ADA1(1'b0),
        .ADB1(1'b0),
        .ADA2(1'b0),
        .ADB2(1'b0),
        .ADA3(write_address[0]),
        .ADB3(read_address[0]),
        .ADA4(write_address[1]),
        .ADB4(read_address[1]),
        .ADA5(write_address[2]),
        .ADB5(read_address[2]),
        .ADA6(write_address[3]),
        .ADB6(read_address[3]),
        .ADA7(write_address[4]),
        .ADB7(read_address[4]),
        .ADA8(write_address[5]),
        .ADB8(read_address[5]),
        .ADA9(write_address[6]),
        .ADB9(read_address[6]),
        .ADA10(write_address[7]),
        .ADB10(read_address[7]),
        .ADA11(write_address[8]),
        .ADB11(read_address[8]),
        .ADA12(write_address[9]),
        .ADB12(read_address[9]),
        .CLKA(write_clk),
        .CLKB(read_clk),
        .CEA(write_enable),
        .CEB(read_enable),
        .OCEA(1'b1),
        .OCEB(1'b1),
        .WEA(1'b1),
        .WEB(1'b0),
        .RSTA(1'b0),
        .RSTB(1'b0),
        .CSA0(1'b0),
        .CSA1(1'b0),
        .CSA2(1'b0),
        .CSB0(1'b0),
        .CSB1(1'b0),
        .CSB2(1'b0)
    );
`else
    reg [7:0] bytes[0:1023];
    reg [7:0] value;
    always @(posedge write_clk)if(write_enable)bytes[write_address]<=write_data;
    always @(posedge read_clk)if(read_enable)value<=bytes[read_address];
    assign read_data=value;
`endif
`undef UJ11_PAL_EBR
endmodule
