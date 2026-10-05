`timescale 1ns/1ps
// 48 MHz reference -> registered 3/6/12/24 MHz clock, without a clock mux.
// A request is committed on a rising edge after an entire old clock period.
module bk_cpu_clock (
    input wire clk48, power_on,
    input wire [1:0] requested_mode,
    input wire bus_idle,
    output reg clk_p = 0,
    output wire clk_n,
    output reg [1:0] active_mode = 0
);
    reg [3:0] count = 0;
    wire [3:0] half_period = 4'd8 >> active_mode;
    assign clk_n = !clk_p;
    always @(posedge clk48) begin
        if (power_on) begin
            count <= 0;
            clk_p <= 0;
            active_mode <= 0;
        end else if (count == half_period - 1'b1) begin
            count <= 0;
            clk_p <= !clk_p;
            if (!clk_p && bus_idle) active_mode <= requested_mode;
        end else count <= count + 1'b1;
    end
endmodule
