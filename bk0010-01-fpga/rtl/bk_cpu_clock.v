`timescale 1ns/1ps
// 48 MHz reference -> registered 3/4/6/8/12/16/24 MHz clock.
// A request is committed on a rising edge after an entire old clock period.
module bk_cpu_clock (
    input wire clk48, power_on,
    input wire [2:0] requested_mode,
    input wire bus_idle,
    output reg clk_p = 0,
    output wire clk_n,
    output reg [2:0] active_mode = 0,
    input wire stall
);
    reg [3:0] count = 0;
    reg [1:0] stall_sync = 0 /* synthesis syn_preserve=1 */;
    // /3 at 16 MHz uses one high and two low reference cycles. Both
    // phases are at least as long as the supported 24 MHz phase.
    wire [3:0] half_period = active_mode == 0 ? 4'd8 :
                             active_mode == 1 ? 4'd6 :
                             active_mode == 2 ? 4'd4 :
                             active_mode == 3 ? 4'd3 :
                             active_mode == 4 ? 4'd2 :
                             active_mode == 5 ? (clk_p ? 4'd1 : 4'd2) : 4'd1;
    assign clk_n = !clk_p;
    always @(posedge clk48) begin
        if (power_on) begin
            count <= 0;
            clk_p <= 0;
            active_mode <= 0;
            stall_sync <= 0;
        end else begin
            stall_sync <= {stall_sync[0],stall};
            if (count == half_period - 1'b1) begin
                // Finish both minimum phase widths before stretching the low
                // phase. Slow disk replies then cannot trip VM1's bus timer.
                if (clk_p || !stall_sync[1]) begin
                    count <= 0;
                    clk_p <= !clk_p;
                    if (!clk_p && bus_idle) active_mode <= requested_mode <= 6 ? requested_mode : 0;
                end
            end else count <= count + 1'b1;
        end
    end
endmodule
