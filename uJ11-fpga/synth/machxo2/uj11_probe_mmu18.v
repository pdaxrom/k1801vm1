`timescale 1ns/1ps
// Isolated cost measurement, not a full MMU or a board-fit claim.
// 46 stimulus FF and 24 observation FF before synthesis merging.
module uj11_probe_mmu18(input wire clk,reset,serial_in,output wire serial_out);
    reg [45:0] stimulus;
    reg [23:0] observe;
    wire [17:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    uj11_mmu_translate18 mmu(.enabled(stimulus[45]),.writing(stimulus[44]),
        .invalid_mode(stimulus[43]),.virtual_address(stimulus[42:27]),
        .par(stimulus[26:15]),.pdr(stimulus[14:0]),
        .physical_address(physical_address),.abort_flags(abort_flags),
        .ram_selected(ram_selected),.io_selected(io_selected),.nxm(nxm));
    always @(posedge clk) begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin
            stimulus<={stimulus[44:0],serial_in};
            observe<={physical_address,abort_flags,ram_selected,io_selected,nxm};
        end
    end
    assign serial_out=^observe;
endmodule
