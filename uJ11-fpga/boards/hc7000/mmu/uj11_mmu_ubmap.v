`timescale 1ns/1ps
// 32 UNIBUS map registers at 170200..170376. One EBR, shared by CSR
// accesses and the disk DMA translator. Page 31 always selects the I/O page;
// DMA outside installed SRAM is completed with NXM, never truncated/aliased.
module uj11_mmu_ubmap(
    input wire clk,reset,enabled,
    input wire request,write,
    input wire [5:0] address,
    input wire [1:0] lanes,
    input wire [15:0] wdata,
    output reg [15:0] rdata,
    output wire ready,
    input wire dma_request,
    input wire [21:0] dma_address,
    output wire dma_ready,dma_error,
    output wire memory_request,
    output wire [21:0] memory_address,
    input wire memory_ready
);
    localparam IDLE=0,CPU_READ=1,CPU_UPDATE=2,CPU_HOLD=3,
        DMA_LOW=4,DMA_HIGH=5,DMA_SUM=6,DMA_BEAT=7;
    reg [2:0] state;
    reg [63:0] valid;
    reg [5:0] csr;
    reg wr;reg [1:0] byte_lanes;reg [15:0] data,low;
    reg [4:0] page;reg [12:0] offset;
    reg [21:0] translated;
    wire [15:0] ram_data;
    wire [15:0] old=valid[csr] ? ram_data : 16'b0;
    wire [15:0] mask={{8{byte_lanes[1]}},{8{byte_lanes[0]}}};
    wire [15:0] merged=((old & ~mask) | (data & mask)) & (csr[0] ? 16'o77 : 16'o177776);
    wire ram_enable=state==CPU_READ || (state==CPU_UPDATE && wr) || state==DMA_LOW || state==DMA_HIGH;
    wire [6:0] ram_address=(state==DMA_LOW || state==DMA_HIGH) ? {1'b0,page,state==DMA_HIGH} : {1'b0,csr};
    uj11_mmu_apr_ram map_ram(.clk(clk),.enable(ram_enable),.address(ram_address),
        .write_enable({2{state==CPU_UPDATE && wr}}),.write_data(merged),.read_data(ram_data));
    assign ready=request && state==CPU_HOLD;
    wire fault=translated[21]; // physical SRAM is exactly 2 MiB
    assign memory_request=dma_request && (!enabled || (state==DMA_BEAT && !fault));
    assign memory_address=enabled ? translated : dma_address;
    assign dma_ready=dma_request && (enabled ? state==DMA_BEAT && (fault || memory_ready) : memory_ready);
    assign dma_error=enabled && state==DMA_BEAT && fault;
    always @(posedge clk)begin
        if(reset)begin state<=IDLE;valid<=0;rdata<=0;csr<=0;wr<=0;byte_lanes<=0;data<=0;low<=0;page<=0;offset<=0;translated<=0;end
        else case(state)
            IDLE:if(request)begin csr<=address;wr<=write;byte_lanes<=lanes;data<=wdata;state<=CPU_READ;end
                else if(dma_request && enabled)begin page<=dma_address[17:13];offset<=dma_address[12:0];state<=DMA_LOW;end
            CPU_READ:state<=CPU_UPDATE;
            CPU_UPDATE:begin rdata<=old;if(wr)valid[csr]<=1;state<=CPU_HOLD;end
            CPU_HOLD:if(!request)state<=IDLE;
            DMA_LOW:state<=DMA_HIGH;
            DMA_HIGH:begin low<=valid[{page,1'b0}] ? ram_data : 16'b0;state<=DMA_SUM;end
            DMA_SUM:begin
                translated<=page==31 ? {9'h1ff,offset} :
                    {(valid[{page,1'b1}] ? ram_data[5:0] : 6'b0),low}+{9'b0,offset};
                state<=DMA_BEAT;
            end
            DMA_BEAT:if(!dma_request)state<=IDLE;
            default:state<=IDLE;
        endcase
    end
endmodule
