`timescale 1ns/1ps
// Isolated resource/timing probe: 52 stimulus + 28 observation FF before
// synthesis merges unused inputs. Translator itself is entirely combinational.
module uj11_probe_mmu(input wire clk,reset,serial_in,output wire serial_out);
    reg [51:0] stimulus;
    reg [27:0] observe;
    wire [21:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    uj11_mmu_translate mmu(.enabled(stimulus[51]),.map22(stimulus[50]),
        .writing(stimulus[49]),.invalid_mode(stimulus[48]),
        .virtual_address(stimulus[47:32]),.par(stimulus[31:16]),.pdr(stimulus[15:0]),
        .physical_address(physical_address),.abort_flags(abort_flags),
        .ram_selected(ram_selected),.io_selected(io_selected),.nxm(nxm));
    always @(posedge clk) begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin
            stimulus<={stimulus[50:0],serial_in};
            observe<={physical_address,abort_flags,ram_selected,io_selected,nxm};
        end
    end
    assign serial_out=^observe;
endmodule
