`timescale 1ns/1ps
// Alternate video with CPU/disk slots; CPU and disk alternate their slots.
// Unused slots are donated immediately. LOCK excludes both DMA masters.
// With FAST_RESPONSE SRAM, request fall withdraws ready immediately and
// allows the next grant while the previous pin release cycle completes.
module uj11_video_arbiter #(parameter FAST_TURNAROUND=0)(
    input wire clk, reset,
    input wire cpu_request, cpu_write, cpu_lock,
    input wire [19:0] cpu_address,
    input wire [1:0] cpu_lanes,
    input wire [15:0] cpu_data,
    output wire cpu_ready,
    input wire dma_request, dma_write,
    input wire [21:0] dma_address,
    input wire [15:0] dma_data,
    input wire [1:0] dma_lanes,
    output wire dma_ready,
    input wire video_request,
    input wire [19:0] video_address,
    output wire video_ready,
    output wire request, output reg write,
    output reg [19:0] address,
    output reg [1:0] lanes,
    output reg [15:0] data,
    input wire ready
);
    localparam IDLE=0, CPU=1, DMA=2, VIDEO=3, RELEASE=4;
    reg [2:0] state;
    reg video_credit;
    reg last_dma;
    reg completed;
    wire owned=state==CPU || state==DMA || state==VIDEO;
    wire owner_request=state==CPU ? cpu_request : state==DMA ? dma_request : video_request;
    // Finish accepted transactions even if a client withdraws its request
    // (e.g. video PLL loss). Address/data remain frozen until SRAM responds.
    assign request=owned && (!completed || owner_request);
    assign cpu_ready=state==CPU && cpu_request && ready;
    assign dma_ready=state==DMA && dma_request && ready;
    assign video_ready=state==VIDEO && video_request && ready;
    wire released=(state==CPU && !cpu_request) || (state==DMA && !dma_request) ||
        (state==VIDEO && !video_request);
    wire arbitrate=state==IDLE || (FAST_TURNAROUND && released && completed && !ready);
    always @(posedge clk) begin
        if(reset) begin
            state<=IDLE;last_dma<=1;video_credit<=1;completed<=0;
            address<=0;data<=0;lanes<=0;write<=0;
        end else begin
            if(owned && ready)completed<=1;
            if(arbitrate)begin
                state<=IDLE;completed<=0;
                if(video_request && !cpu_lock && (video_credit || !(cpu_request || dma_request))) begin
                    state<=VIDEO;video_credit<=0;
                    address<=video_address;data<=0;lanes<=3;write<=0;
                end else if(dma_request && !cpu_lock && (!cpu_request || !last_dma)) begin
                    state<=DMA;last_dma<=1;video_credit<=1;
                    address<=dma_address[20:1];data<=dma_data;lanes<=dma_lanes;write<=dma_write;
                end else if(cpu_request) begin
                    state<=CPU;last_dma<=0;video_credit<=1;
                    address<=cpu_address;data<=cpu_data;lanes<=cpu_lanes;write<=cpu_write;
                end
            end else case(state)
                CPU,DMA,VIDEO: if(released && completed)state<=RELEASE;
                RELEASE: if(!ready)state<=IDLE;
                default: state<=IDLE;
            endcase
        end
    end
endmodule
