`timescale 1ns/1ps
// DEC EK-DCJ11-UG-PRE 4.5.2, 4.7.4.3 and figures 4-11..4-13.
// Stateless translation/checking only. No CPU connection or MMR/PAR/PDR store.
module uj11_mmu_translate (
    input wire enabled, map22, writing, invalid_mode,
    input wire [15:0] virtual_address, par, pdr,
    output wire [21:0] physical_address,
    output wire [2:0] abort_flags,
    // Candidate address classes: caller MUST inhibit bus requests on abort.
    output wire ram_selected, io_selected, nxm
);
    wire unused_pdr = &{pdr[15],pdr[7:4],pdr[0]};
    // A 16-bit block sum wraps at 4 MiB; its low 12 bits also wrap at 256 KiB.
    wire [15:0] block_address = par + {9'b0,virtual_address[12:6]};
    wire io18 = &block_address[11:7];
    wire io16 = &virtual_address[15:13];
    wire wide = enabled && map22;
    wire legacy_io = enabled ? io18 : io16;
    // Factor width/enable once before the four high address bits. Decode the
    // address classes from the block sum, before the physical-address mux.
    assign physical_address[21:18] = wide ? block_address[15:12] : {4{legacy_io}};
    assign physical_address[17:16] = enabled ? block_address[11:10] : {2{io16}};
    assign physical_address[15:6] = enabled ? block_address[9:0] : virtual_address[15:6];
    assign physical_address[5:0] = virtual_address[5:0];
    wire [7:0] length_delta = {1'b0,virtual_address[12:6]} - {1'b0,pdr[14:8]};
    assign abort_flags[2] = enabled && (!pdr[1] || invalid_mode);
    assign abort_flags[1] = enabled && (pdr[3] ? length_delta[7] :
        (!length_delta[7] && (|length_delta[6:0])));
    assign abort_flags[0] = enabled && writing && pdr[2:1] == 2'b01;
    assign ram_selected = enabled ?
        !(block_address[11] || (map22 && (|block_address[15:12]))) : !io16;
    assign io_selected = enabled ?
        (io18 && (!map22 || (&block_address[15:12]))) : io16;
    assign nxm = !ram_selected && !io_selected;
endmodule
