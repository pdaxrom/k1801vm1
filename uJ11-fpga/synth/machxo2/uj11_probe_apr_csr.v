`timescale 1ns/1ps
// APR + canonical physical CSR decode. 43 stimulus + 19 observation FF;
// no CPU, MMR state, automatic translation, or permission enforcement.
module uj11_probe_apr_csr(input wire clk,reset,serial_in,output wire serial_out);
    reg [42:0] stimulus;
    reg [18:0] observe;
    wire [15:0] data;
    wire ready,busy,selected,pdr_select;
    wire [5:0] entry;
    uj11_mmu_apr_decode decode(.physical_address(stimulus[41:20]),
        .selected(selected),.pdr_select(pdr_select),.entry(entry));
    uj11_mmu_apr apr(.clk(clk),.reset(reset),.request(stimulus[42] && selected),
        .entry(entry),.pdr_select(pdr_select),.writing(stimulus[19]),
        .mark_written(stimulus[18]),.byte_enable(stimulus[17:16]),
        .write_data(stimulus[15:0]),.read_data(data),.ready(ready),.busy(busy));
    always @(posedge clk)begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin stimulus<={stimulus[41:0],serial_in};observe<={data,ready,busy,selected};end
    end
    assign serial_out=^observe;
endmodule
