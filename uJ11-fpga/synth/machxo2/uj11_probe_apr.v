`timescale 1ns/1ps
// Isolated APR gate. 28 stimulus and 18 observation FF are measurement only.
module uj11_probe_apr(input wire clk,reset,serial_in,output wire serial_out);
    reg [27:0] stimulus;
    reg [17:0] observe;
    wire [15:0] data;
    wire ready,busy;
    uj11_mmu_apr apr(.clk(clk),.reset(reset),.request(stimulus[27]),
        .entry(stimulus[26:21]),.pdr_select(stimulus[20]),.writing(stimulus[19]),
        .mark_written(stimulus[18]),.byte_enable(stimulus[17:16]),
        .write_data(stimulus[15:0]),.read_data(data),.ready(ready),.busy(busy));
    always @(posedge clk)begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin stimulus<={stimulus[26:0],serial_in};observe<={data,ready,busy};end
    end
    assign serial_out=^observe;
endmodule
