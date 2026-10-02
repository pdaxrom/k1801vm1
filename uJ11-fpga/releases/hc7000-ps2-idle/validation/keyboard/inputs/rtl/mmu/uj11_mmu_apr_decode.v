`timescale 1ns/1ps
// DEC EK-DCJ11-UG-PRE 4.9: only canonical PA22 addresses are accepted.
// Supervisor 17772200..17772277, kernel 17772300..17772377,
// user 17777600..17777677, including both byte lanes. Odd-word errors and
// access privilege are decided before this decode by the CPU/bus controller.
module uj11_mmu_apr_decode(input wire [21:0] physical_address,
    output wire selected, pdr_select, output wire [5:0] entry);
    wire unused_byte_lane=physical_address[0]; // caller selects byte lanes
    assign selected=(&physical_address[21:12]) &&
        ((physical_address[11:7]==5'b01001) || (physical_address[11:6]==6'b111110));
    // entry={mode,I/D,page}; mode2 has storage slots but no architectural CSR.
    assign entry={physical_address[11],!physical_address[6],physical_address[4:1]};
    assign pdr_select=!physical_address[5];
endmodule
