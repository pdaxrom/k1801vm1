`timescale 1ns/1ps
// J11 User Guide 1.3, table 1-3: explicit writes preserve T; PS<10:9>=0.
// uJ11 retains mode/RS metadata; it does not implement alternate RF or MMU.
module uj11_psw (
    input wire clk, reset, enable,
    input wire [1:0] update,
    input wire [3:0] nzvc,
    input wire [15:0] value,
    input wire bus_write, bus_byte, bus_odd,
    input wire [15:0] bus_value,
    output reg [15:0] psw
);
    always @(posedge clk) begin
        if (reset) psw <= 16'o000340;
        else if (enable) begin
            if (bus_write) begin
                if (!bus_byte || !bus_odd) begin
                    psw[7:5] <= bus_value[7:5];
                    psw[3:0] <= bus_value[3:0];
                end
                if (!bus_byte || bus_odd)
                    psw[15:8] <= (bus_byte ? bus_value[7:0] : bus_value[15:8]) & 8'hf9;
            end else case (update)
                2'd1: psw[3:1] <= nzvc[3:1];
                2'd2: psw[3:0] <= nzvc;
                2'd3: psw <= value & 16'hf9ff;
                default: ;
            endcase
        end
    end
endmodule
