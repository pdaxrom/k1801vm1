`timescale 1ns/1ps
// One DP8KC, two disjoint x9 byte lanes. 128 x 16 used words, synchronous
// read, no output register. Writes do not define read_data until a later read.
// No reset of memory contents. FPGA configuration initializes the array to 0.
module uj11_mmu_apr_ram(input wire clk, enable,
    input wire [6:0] address, input wire [1:0] write_enable,
    input wire [15:0] write_data, output wire [15:0] read_data);
`ifdef SYNTHESIS
`define UJ11_APR_EBR
`elsif UJ11_VENDOR_ROM
`define UJ11_APR_EBR
`endif
`ifdef UJ11_APR_EBR
    DP8KC #(.DATA_WIDTH_A(9), .DATA_WIDTH_B(9),
        .REGMODE_A("NOREG"), .REGMODE_B("NOREG"),
        .WRITEMODE_A("NORMAL"), .WRITEMODE_B("NORMAL"),
        .CSDECODE_A("0b000"), .CSDECODE_B("0b000"),
        .GSR("DISABLED"), .RESETMODE("SYNC"),
        .ASYNC_RESET_RELEASE("SYNC"), .INIT_DATA("STATIC")) memory (
        .DIA0(write_data[0]), .DIA1(write_data[1]), .DIA2(write_data[2]),
        .DIA3(write_data[3]), .DIA4(write_data[4]), .DIA5(write_data[5]),
        .DIA6(write_data[6]), .DIA7(write_data[7]), .DIA8(1'b0),
        .DIB0(write_data[8]), .DIB1(write_data[9]), .DIB2(write_data[10]),
        .DIB3(write_data[11]), .DIB4(write_data[12]), .DIB5(write_data[13]),
        .DIB6(write_data[14]), .DIB7(write_data[15]), .DIB8(1'b0),
        .ADA0(1'b1), .ADA1(1'b0), .ADA2(1'b0), .ADA3(1'b0),
        .ADA4(address[0]), .ADA5(address[1]), .ADA6(address[2]),
        .ADA7(address[3]), .ADA8(address[4]), .ADA9(address[5]), .ADA10(address[6]),
        .ADA11(1'b0), .ADA12(1'b0),
        .ADB0(1'b1), .ADB1(1'b0), .ADB2(1'b0), .ADB3(1'b1),
        .ADB4(address[0]), .ADB5(address[1]), .ADB6(address[2]),
        .ADB7(address[3]), .ADB8(address[4]), .ADB9(address[5]), .ADB10(address[6]),
        .ADB11(1'b0), .ADB12(1'b0),
        .CEA(enable), .CEB(enable), .OCEA(enable), .OCEB(enable),
        .CLKA(clk), .CLKB(clk), .WEA(write_enable[0]), .WEB(write_enable[1]),
        .RSTA(1'b0), .RSTB(1'b0), .CSA0(1'b0), .CSA1(1'b0), .CSA2(1'b0),
        .CSB0(1'b0), .CSB1(1'b0), .CSB2(1'b0),
        .DOA0(read_data[0]), .DOA1(read_data[1]), .DOA2(read_data[2]),
        .DOA3(read_data[3]), .DOA4(read_data[4]), .DOA5(read_data[5]),
        .DOA6(read_data[6]), .DOA7(read_data[7]), .DOA8(),
        .DOB0(read_data[8]), .DOB1(read_data[9]), .DOB2(read_data[10]),
        .DOB3(read_data[11]), .DOB4(read_data[12]), .DOB5(read_data[13]),
        .DOB6(read_data[14]), .DOB7(read_data[15]), .DOB8());
`undef UJ11_APR_EBR
`else
    reg [7:0] low[0:127], high[0:127];
    reg [15:0] data;
    integer i;
    initial for(i=0;i<128;i=i+1)begin low[i]=0;high[i]=0;end
    always @(posedge clk)if(enable)begin
        if(write_enable[0])low[address]<=write_data[7:0];
        else data[7:0]<=low[address];
        if(write_enable[1])high[address]<=write_data[15:8];
        else data[15:8]<=high[address];
    end
    assign read_data=data;
`endif
endmodule
