`timescale 1ns/1ps
module bk_boot_rom #(parameter INIT_FILE = "build/boot.hex") (
    input wire clk,
    input wire [11:0] address,
    output reg [15:0] data
);
    reg [15:0] rom[0:4095];
    initial $readmemh(INIT_FILE, rom);
    always @(posedge clk) data <= rom[address];
endmodule
