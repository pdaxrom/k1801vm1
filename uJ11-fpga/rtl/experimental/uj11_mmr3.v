`timescale 1ns/1ps
// DEC DCJ11 User's Guide 4.7.4: six writable bits, INIT/RESET clears.
// CP43 CSR storage only. Consumers of map22, split I/D, CSM and MAP are
// deliberately absent from the experimental CPU until separately verified.
module uj11_mmr3 (
    input wire clk, reset, request, writing, low_byte_enable,
    input wire [5:0] write_data,
    output reg [5:0] value,
    output reg ready
);
    // A registered acknowledgement remains asserted until request release.
    // It also prevents repeated writes while the accepted request is held.
    always @(posedge clk) begin
        if (reset) begin
            value <= 6'b0;
            ready <= 1'b0;
        end else begin
            ready <= request;
            if (request && !ready && writing && low_byte_enable) value <= write_data;
        end
    end
endmodule
