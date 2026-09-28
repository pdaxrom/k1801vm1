`timescale 1ns/1ps
// A 512-byte staging RAM and autonomous sector transfers. SPI commands/tokens
// belong to firmware. Operations: 1=SPI->buffer, 2=buffer->SPI,
// 3=physical SRAM->buffer (zero-pad), 4=buffer->physical SRAM (stop at word count).
module uj11_mmu_sector_engine(
    input wire clk, reset, abort,
    input wire reg_write,
    input wire [2:0] reg_address,
    input wire [31:0] reg_data,
    output wire [31:0] reg_rdata,
    output wire busy,
    output wire spi_request, spi_write,
    output wire [7:0] spi_data,
    input wire spi_ready,
    input wire [7:0] spi_rdata,
    output wire dma_request, dma_write,
    output wire [21:0] dma_address,
    output wire [15:0] dma_data,
    input wire dma_ready,dma_error,
    input wire [15:0] dma_rdata
);
    localparam IDLE=0, BUFFER_READ=1, BUFFER_WAIT=2, SPI_WAIT=3,
        SPI_GAP=4, DMA_WAIT=5, DMA_GAP=6, PAD=7;
    reg [2:0] state,mode;
    reg [21:0] base;
    reg [7:0] offset;reg [8:0] limit;reg wide,inhibit,mismatch;
    wire [7:0] relative_index=index-offset;
    wire [7:0] dma_index=inhibit ? 8'b0 : relative_index;
    wire [17:0] short_address=base[17:0]+{9'b0,dma_index,1'b0};
    reg [15:0] crc;
    reg [15:0] direct_data;
    reg [8:0] count;
    reg [7:0] index,low;
    reg high,error,nxm;
    wire [15:0] buffer_data;
    wire buffer_write=(state==SPI_WAIT && spi_ready && mode==1 && high) ||
                      (state==DMA_WAIT && dma_ready && !dma_error && mode==3) || state==PAD;
    wire [15:0] buffer_input=state==PAD ? 16'b0 : mode==1 ? {spi_rdata,low} : dma_rdata;
    uj11_sector_ram buffer(.clk(clk),.write(buffer_write),.address(index),
        .write_data(buffer_input),.data(buffer_data));
    assign busy=state!=IDLE;
    assign spi_request=state==SPI_WAIT;
    assign spi_write=mode==2;
    assign spi_data=high ? buffer_data[15:8] : buffer_data[7:0];
    assign dma_request=state==DMA_WAIT;
    assign dma_write=mode==4 || mode==7;
    assign dma_address=mode>=6 ? base : wide ? base+{13'b0,dma_index,1'b0} : {4'b0,short_address};
    assign dma_data=mode==7 ? direct_data : buffer_data;
    assign reg_rdata=reg_address==0 ? {10'b0,base} :
                     reg_address==1 ? {23'b0,count} : reg_address==5 ? {16'b0,direct_data} :
                     {crc,12'b0,nxm,mismatch,error,busy};
    function [15:0] crc_byte(input [15:0] old, input [7:0] data);
        reg [15:0] v; integer b;
        begin
            v=old;
            for(b=7;b>=0;b=b-1) v={v[14:0],1'b0} ^
                ((v[15]^data[b]) ? 16'h1021 : 16'b0);
            crc_byte=v;
        end
    endfunction
    always @(posedge clk) begin
        if(reset) begin
            state<=IDLE;mode<=0;base<=0;count<=0;index<=0;low<=0;high<=0;crc<=0;error<=0;
            offset<=0;limit<=256;wide<=0;inhibit<=0;mismatch<=0;nxm<=0;
            direct_data<=0;
        end else begin
            if(reg_write && !busy) case(reg_address)
                0:base<=reg_data[21:0];
                1:count<=reg_data[8:0];
                2:begin
                    index<=reg_data[2:0]<=2 ? 0 : offset;high<=0;crc<=0;mode<=reg_data[2:0];error<=0;mismatch<=0;nxm<=0;
                    if(reg_data[2:0]==1) state<=SPI_WAIT;
                    else if(reg_data[2:0]==2) state<=BUFFER_READ;
                    else if(reg_data[2:0]>=6 && !abort && !base[0] && !base[21])state<=DMA_WAIT;
                    else if((reg_data[2:0]>=3 && reg_data[2:0]<=5) && !abort &&
                            !base[0] && count>0 && {1'b0,offset}+count<=limit && limit<=256 &&
                            (!wide || ({1'b0,base}+(inhibit ? 23'd2 : {13'b0,count,1'b0})<=23'h200000)))
                        state<=reg_data[2:0]==3 ? DMA_WAIT : BUFFER_READ;
                    else begin error<=1;nxm<=!abort;end
                end
                3:begin offset<=reg_data[7:0];limit<=reg_data[16:8];end
                4:begin wide<=reg_data[0];inhibit<=reg_data[1];end
                5:direct_data<=reg_data[15:0];
                default:begin end
            endcase
            case(state)
                BUFFER_READ:state<=BUFFER_WAIT;
                BUFFER_WAIT:if(abort && mode!=2)begin state<=IDLE;error<=1;end
                    else state<=mode==2 ? SPI_WAIT : DMA_WAIT;
                SPI_WAIT:if(spi_ready) begin
                    crc<=crc_byte(crc,mode==1 ? spi_rdata : spi_data);
                    if(!high)low<=spi_rdata;
                    state<=SPI_GAP;
                end
                SPI_GAP:begin
                    high<=!high;
                    if(!high)state<=SPI_WAIT;
                    else if(index==255)state<=IDLE;
                    else begin index<=index+1'b1;state<=mode==1 ? SPI_WAIT : BUFFER_READ;end
                end
                DMA_WAIT:if(dma_ready)begin
                    if(mode==6 && !dma_error)direct_data<=dma_rdata;
                    if(mode==5 && dma_rdata!=buffer_data)mismatch<=1;
                    if(dma_error)begin state<=IDLE;error<=1;nxm<=1;end
                    else state<=mode>=6 ? IDLE : DMA_GAP;
                end
                DMA_GAP:begin
                    if(abort)begin state<=IDLE;error<=1;end
                    else if((mode==4 || mode==5) && {1'b0,relative_index}+9'd1==count)state<=IDLE;
                    else if({1'b0,index}+9'd1==limit)state<=IDLE;
                    else begin
                        index<=index+1'b1;
                        state<=mode!=3 ? BUFFER_READ :
                            ({1'b0,relative_index}+9'd1>=count ? PAD : DMA_WAIT);
                    end
                end
                PAD:if({1'b0,index}+9'd1==limit || abort)state<=IDLE;else index<=index+1'b1;
                default:begin end
            endcase
        end
    end
endmodule
