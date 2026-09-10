`timescale 1ns/1ps
// CP44 translation bridge: kernel unified PAR relocation only.
// No PDR check/W, MMR metadata, abort/restart, modes or split I/D.
// CPU holds VA/control/data until acknowledge. The EBR port is released
// after capture, so a translated APR CSR can safely use the same memory.
module uj11_mmu_relocate (
    input wire clk, reset, enabled, map22, request, acknowledge,
    input wire [15:0] virtual_address,
    output wire lookup_request,
    output wire [6:0] lookup_address,
    input wire lookup_grant,
    input wire [15:0] lookup_data,
    output wire translated_request,
    output wire ram_region, io_region,
    output wire [21:0] physical_address
);
    localparam IDLE=2'b00, CAPTURE=2'b01, MAPPED=2'b10, BYPASS=2'b11;
    reg [1:0] phase;
    reg [15:0] block_address;
    reg mapped_ram,mapped_io;
    wire [15:0] block_sum=lookup_data+{9'b0,virtual_address[12:6]};
    wire starting=request && phase==IDLE;
    assign lookup_request=starting && enabled && !reset;
    assign lookup_address={3'b000,virtual_address[15:13],1'b0};
    assign translated_request=request && !reset &&
        ((phase==IDLE && !enabled) || phase[1]);
    assign physical_address=phase==MAPPED ? {block_address,virtual_address[5:0]} :
                                          {{6{&virtual_address[15:13]}},virtual_address};
    // Decode the complete physical region before narrowing the FRAM pins.
    // The board consumes these classifications, avoiding a wide repeated
    // address mux/decode after the holding registers.
    assign ram_region=phase==MAPPED ? mapped_ram : !(&virtual_address[15:13]);
    assign io_region=phase==MAPPED ? mapped_io : (&virtual_address[15:13]);
    always @(posedge clk)begin
        if(reset)phase<=IDLE;
        else if(phase==CAPTURE)begin
            block_address<={(map22 ? block_sum[15:12] : {4{&block_sum[11:7]}}),block_sum[11:0]};
            mapped_ram<=!block_sum[11] && (!map22 || !(|block_sum[15:12]));
            mapped_io<=(&block_sum[11:7]) && (!map22 || (&block_sum[15:12]));
            phase<=MAPPED;
        end else if(starting)begin
            if(enabled)begin if(lookup_grant)phase<=CAPTURE;end
            else if(!acknowledge)phase<=BYPASS;
        end else if(!request || acknowledge)phase<=IDLE;
    end
endmodule
