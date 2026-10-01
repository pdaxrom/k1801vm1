`timescale 1ns/1ps
// MMU-only distributed RAM, two independent byte lanes, 128 x 16 words.
// Synchronous read with the same one-clock interface as the former EBR. Writes do not define read_data until a later read.
// No reset of memory contents. FPGA configuration initializes the array to 0.
module uj11_mmu_apr_ram(input wire clk, enable,
    input wire [6:0] address, input wire [1:0] write_enable,
    input wire [15:0] write_data, output wire [15:0] read_data);
    reg [7:0] low[0:127] /* synthesis syn_ramstyle = "distributed" */;
    reg [7:0] high[0:127] /* synthesis syn_ramstyle = "distributed" */;
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
endmodule
