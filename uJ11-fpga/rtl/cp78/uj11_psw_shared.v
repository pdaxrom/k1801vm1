`timescale 1ns/1ps
// CP78c: the ALU supplies both ordinary LOAD and explicit write data.
// Contract: value==bus_value while bus_write; engine selects PASS B / AB.
module uj11_psw (
    input wire clk, reset, enable,
    input wire [1:0] update,
    input wire [3:0] nzvc,
    input wire [15:0] value,
    input wire bus_write, bus_byte, bus_odd,
    input wire [15:0] bus_value,
    output reg [15:0] psw
);
    wire load = !bus_write && update==2'd3;
    wire flags = !bus_write && (update==2'd1 || update==2'd2);
    wire flag_c = !bus_write && update==2'd2;
    wire low_write = bus_write && (!bus_byte || !bus_odd);
    wire high_word = bus_write && !bus_byte;
    wire high_byte = bus_write && bus_byte && bus_odd;
    wire [3:0] low_data = ({4{load || low_write}} & value[3:0]) |
                          ({4{flags}} & nzvc);
    wire [7:0] high_data = ({8{load || high_word}} & value[15:8]) |
                           ({8{high_byte}} & value[7:0]);
    always @(posedge clk) begin
        if (reset) psw <= 16'o000340;
        else if (enable) begin
            if (load || low_write || flags) psw[3:1] <= low_data[3:1];
            if (load || low_write || flag_c) psw[0] <= low_data[0];
            if (load) psw[4] <= value[4];
            if (load || low_write)
                psw[7:5] <= value[7:5];
            if (load || high_word || high_byte) psw[15:8] <= high_data & 8'hf9;
        end
    end
endmodule
