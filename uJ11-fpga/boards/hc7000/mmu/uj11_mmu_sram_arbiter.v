`timescale 1ns/1ps
// Two masters, one complete SRAM transaction per grant. Alternate contenders;
// neither can steal a held response or change pins during another transfer.
module uj11_mmu_sram_arbiter(
    input wire clk, reset,
    input wire cpu_request, cpu_write,
    input wire [19:0] cpu_address,
    input wire [1:0] cpu_lanes,
    input wire [15:0] cpu_data,
    output wire cpu_ready,
    input wire dma_request, dma_write,
    input wire [17:0] dma_address,
    input wire [15:0] dma_data,
    output wire dma_ready,
    output wire request, write,
    output wire [19:0] address,
    output wire [1:0] lanes,
    output wire [15:0] data,
    input wire ready
);
    localparam IDLE=0, CPU=1, DMA=2, RELEASE=3;
    reg [1:0] state;
    reg last_dma;
    assign request=(state==CPU && cpu_request) || (state==DMA && dma_request);
    assign write=state==DMA ? dma_write : cpu_write;
    // DMA addresses are untranslated 18-bit physical byte addresses (low 256 KiB).
    assign address=state==DMA ? {3'b0,dma_address[17:1]} : cpu_address;
    assign lanes=state==DMA ? 2'b11 : cpu_lanes;
    assign data=state==DMA ? dma_data : cpu_data;
    assign cpu_ready=state==CPU && cpu_request && ready;
    assign dma_ready=state==DMA && dma_request && ready;
    always @(posedge clk) begin
        if(reset) begin state<=IDLE;last_dma<=1;end
        else case(state)
            IDLE: if(dma_request && (!cpu_request || !last_dma)) begin
                state<=DMA;last_dma<=1;
            end else if(cpu_request) begin state<=CPU;last_dma<=0;end
            CPU: if(!cpu_request) state<=RELEASE;
            DMA: if(!dma_request) state<=RELEASE;
            RELEASE: if(!ready) state<=IDLE;
        endcase
    end
endmodule
