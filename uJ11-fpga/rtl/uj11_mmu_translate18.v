`timescale 1ns/1ps
// First MMU resource probe; NOT connected to the production CPU yet.
// DEC EK-DCJ11-UG-PRE sections 4.5.2, 4.7.1, 4.7.4.3, figure 4-12.
// PAR/PDR selection, CSR storage, W bit and abort/restart belong to the caller.
module uj11_mmu_translate18 (
    input wire enabled, writing, invalid_mode,
    input wire [15:0] virtual_address,
    input wire [11:0] par,
    input wire [14:0] pdr,
    output wire [17:0] physical_address,
    // Independent MMR0 error bits <15:13>: nonresident, length, read-only.
    output wire [2:0] abort_flags,
    // Board has 128 KiB populated. NXM must NEVER alias RAM bit16..0.
    output wire ram_selected, io_selected, nxm
);
    wire unused_pdr = &{pdr[7:4],pdr[0]};
    wire [11:0] block_address = par + {5'b0,virtual_address[12:6]};
    wire [17:0] translated = {block_address,virtual_address[5:0]};
    // One subtractor supplies both ordering and equality, shared by ED=0/1.
    wire [7:0] length_delta = {1'b0,virtual_address[12:6]} - {1'b0,pdr[14:8]};
    wire unmapped_io = &virtual_address[15:13];
    assign physical_address = enabled ? translated :
                              {{2{unmapped_io}},virtual_address};
    // DCJ11 ACF is TWO bits. PDR bit0 is reserved, not an access-trap bit.
    assign abort_flags[2] = enabled && (!pdr[1] || invalid_mode);
    assign abort_flags[1] = enabled && (pdr[3] ? length_delta[7] :
        (!length_delta[7] && (|length_delta[6:0])));
    assign abort_flags[0] = enabled && writing && pdr[2:1] == 2'b01;
    assign io_selected = &physical_address[17:13];
    assign ram_selected = !physical_address[17];
    assign nxm = physical_address[17] && !io_selected;
endmodule
