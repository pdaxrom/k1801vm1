`timescale 1ns/1ps
// One beat per request && ack edge. The engine holds the microinstruction,
// address and write data until completion; no duplicate address registers.
// Byte data is right justified and addr retains bit 0. No address translation.
module uj11_mem (
    input wire active, writing, byte_access,
    input wire [15:0] address, data,
    input wire ack, error,
    output wire request, read, write, byte_word,
    output wire [15:0] addr, write_data,
    output wire complete,
    output wire [1:0] fault
);
    wire odd_word = !byte_access && address[0];
    assign request = active && !odd_word;
    assign read = request && !writing;
    assign write = request && writing;
    assign byte_word = byte_access;
    assign addr = address;
    assign write_data = data;
    assign complete = active && (odd_word || ack);
    assign fault = !active ? 2'd0 : odd_word ? 2'd1 :
                   (ack && error) ? 2'd2 : 2'd0;
endmodule
