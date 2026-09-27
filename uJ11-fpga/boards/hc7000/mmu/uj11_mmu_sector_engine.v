`timescale 1ns/1ps
// A 512-byte staging RAM and autonomous sector transfers. SPI commands/tokens
// belong to firmware. Operations: 1=SPI->buffer, 2=buffer->SPI,
// 3=physical SRAM->buffer (zero-pad), 4=buffer->physical SRAM (stop at word count).
module uj11_mmu_sector_engine(
    input wire clk, reset,
    input wire reg_write,
    input wire [1:0] reg_address,
    input wire [31:0] reg_data,
    output wire [31:0] reg_rdata,
    output wire busy,
    output wire spi_request, spi_write,
    output wire [7:0] spi_data,
    input wire spi_ready,
    input wire [7:0] spi_rdata,
    output wire dma_request, dma_write,
    output wire [17:0] dma_address,
    output wire [15:0] dma_data,
    input wire dma_ready,
    input wire [15:0] dma_rdata
);
    localparam IDLE=0, BUFFER_READ=1, BUFFER_WAIT=2, SPI_WAIT=3,
        SPI_GAP=4, DMA_WAIT=5, DMA_GAP=6, PAD=7;
    reg [2:0] state,mode;
    reg [17:0] base;
    reg [15:0] crc;
    reg [8:0] count;
    reg [7:0] index,low;
    reg high,error;
    wire [15:0] buffer_data;
    wire buffer_write=(state==SPI_WAIT && spi_ready && mode==1 && high) ||
                      (state==DMA_WAIT && dma_ready && mode==3) || state==PAD;
    wire [15:0] buffer_input=state==PAD ? 16'b0 : mode==1 ? {spi_rdata,low} : dma_rdata;
    uj11_sector_ram buffer(.clk(clk),.write(buffer_write),.address(index),
        .write_data(buffer_input),.data(buffer_data));
    assign busy=state!=IDLE;
    assign spi_request=state==SPI_WAIT;
    assign spi_write=mode==2;
    assign spi_data=high ? buffer_data[15:8] : buffer_data[7:0];
    assign dma_request=state==DMA_WAIT;
    assign dma_write=mode==4;
    assign dma_address=base+{9'b0,index,1'b0};
    assign dma_data=buffer_data;
    assign reg_rdata=reg_address==0 ? {14'b0,base} :
                     reg_address==1 ? {23'b0,count} : {crc,14'b0,error,busy};
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
        end else begin
            if(reg_write && !busy) case(reg_address)
                0:base<=reg_data[17:0];
                1:count<=reg_data[8:0];
                2:begin
                    index<=0;high<=0;crc<=0;mode<=reg_data[2:0];error<=0;
                    if(reg_data[2:0]==1) state<=SPI_WAIT;
                    else if(reg_data[2:0]==2) state<=BUFFER_READ;
                    else if((reg_data[2:0]==3 || reg_data[2:0]==4) &&
                            !base[0] && count>0 && count<=256)
                        state<=reg_data[2:0]==3 ? DMA_WAIT : BUFFER_READ;
                    else error<=1;
                end
                default:begin end
            endcase
            case(state)
                BUFFER_READ:state<=BUFFER_WAIT;
                BUFFER_WAIT:state<=mode==2 ? SPI_WAIT : DMA_WAIT;
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
                DMA_WAIT:if(dma_ready)state<=DMA_GAP;
                DMA_GAP:begin
                    if(mode==4 && {1'b0,index}+9'd1==count)state<=IDLE;
                    else if(index==255)state<=IDLE;
                    else begin
                        index<=index+1'b1;
                        state<=mode==4 ? BUFFER_READ :
                            ({1'b0,index}+9'd1>=count ? PAD : DMA_WAIT);
                    end
                end
                PAD:if(index==255)state<=IDLE;else index<=index+1'b1;
                default:begin end
            endcase
        end
    end
endmodule
