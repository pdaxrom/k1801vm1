`timescale 1ns/1ps
// No keyboard on this board. Preserve host SELECT until it really drops,
// even after the guest releases TDO between blocks of one HG disk request.
module uj11_hg_inputs(
    input wire clk, reset, output_enable,
    input wire [3:0] rows,
    output wire [3:0] filtered
);
    reg [1:0] select_sync;
    reg active;
    always @(posedge clk) begin
        if(reset) begin select_sync<=0; active<=0; end
        else begin
            select_sync<={select_sync[0],rows[0]};
            if(output_enable) active<=1;
            else if(!select_sync[1]) active<=0;
        end
    end
    assign filtered=(output_enable || active) ? rows : 4'b0;
endmodule
