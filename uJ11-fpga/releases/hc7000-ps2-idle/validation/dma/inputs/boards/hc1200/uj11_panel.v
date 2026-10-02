`timescale 1ns/1ps
// HC1200 CSR 166000/166001. Software owns HCMS, RGB, keyboard and HG timing.
// JTAG/keyboard inputs are asynchronous to OSCH; only the second stage is read.
module uj11_panel (
    input wire clk, reset, write_enable,
    input wire [3:0] rows,
    input wire [7:0] write_data,
    output reg [7:0] pins,
    output wire [15:0] read_data
);
    reg [3:0] rows_meta, rows_sync;
    always @(posedge clk) begin
        if (reset) begin
            rows_meta <= 0;
            rows_sync <= 0;
            pins <= 8'h12; // CE high, display blanked, HG TDO high impedance.
        end else begin
            rows_meta <= rows;
            rows_sync <= rows_meta;
            if (write_enable) pins <= write_data;
        end
    end
    assign read_data = {pins, rows_sync, 4'b0};
endmodule
