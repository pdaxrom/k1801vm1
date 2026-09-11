// UJ11_MMU is opt-in: undefined = CP40h, defined = experimental CP47c.
`timescale 1ns/1ps
// Board-only held-request transport, SPI mode 0. Address/data/control must
// remain stable through ready; the uJ11 engine provides that contract.
// Bank is a private peripheral-storage bit, not CPU address translation.
module uj11_board_fram #(parameter integer CLK_DIV=1)(
    input wire clk,rst,req,write,byte_access,bank,
    input wire [15:0] address,wdata,
    output reg [15:0] rdata,
    output reg ready,error,
    output wire busy,
    output reg spi_cs_n,spi_sck,spi_mosi,
    input wire spi_miso
);
    localparam [3:0] IDLE=0,WREN=1,GAP=2,CMD=3,BANK=4,
                     HIGH=5,LOW=6,DATA_LO=7,DATA_HI=8,DONE=9;
    localparam integer DIV_WIDTH=CLK_DIV>1?$clog2(CLK_DIV):1;
    localparam integer DIV_LAST=CLK_DIV-1;
    reg [3:0] state;
    reg active,seen;
    reg [6:0] tx;
`ifdef UJ11_MMU
    // rdata is valid at ready and stable while idle. During a transfer,
    // its high byte is the serial receive shift register (no separate rx FFs).
    wire [7:0] rx=rdata[15:8];
`else
    reg [7:0] rx;
`endif
    reg [2:0] bit_count;
    reg [DIV_WIDTH-1:0] divider;
    reg [7:0] next_byte;
    assign busy=state!=IDLE;
    always @*case(state)
        WREN:next_byte=8'h06;
        CMD:next_byte={7'b0000001,!write};
        BANK:next_byte={7'b0,bank};
        HIGH:next_byte=address[15:8];
        LOW:next_byte=address[7:0];
        DATA_LO:next_byte=wdata[7:0];
        DATA_HI:next_byte=wdata[15:8];
        default:next_byte=0;
    endcase
    always @(posedge clk)begin
        if(rst)begin
            state<=IDLE;active<=0;seen<=0;rdata<=0;ready<=0;error<=0;
            spi_cs_n<=1;spi_sck<=0;spi_mosi<=0;
`ifdef UJ11_MMU
            tx<=0;bit_count<=0;divider<=0;
`else
            tx<=0;rx<=0;bit_count<=0;divider<=0;
`endif
        end else begin
            ready<=0;error<=0;
            if(!req)seen<=0;
            if(active)begin
                if(divider==DIV_LAST[DIV_WIDTH-1:0])begin
                    divider<=0;
                    spi_sck<=!spi_sck;
`ifdef UJ11_MMU
                    if(!spi_sck)rdata[15:8]<={rx[6:0],spi_miso};
`else
                    if(!spi_sck)rx<={rx[6:0],spi_miso};
`endif
                    else if(bit_count==7)begin
                        active<=0;
                        if(state==DATA_LO)begin
                            rdata[7:0]<=rx;
                            if(byte_access)rdata[15:8]<=0;
                        end
`ifdef UJ11_MMU
`else
                        if(state==DATA_HI)rdata[15:8]<=rx;
`endif
                        state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;
                    end else begin
                        bit_count<=bit_count+1'b1;
                        spi_mosi<=tx[6];tx<={tx[5:0],1'b0};
                    end
                end else divider<=divider+1'b1;
            end else case(state)
                IDLE:begin
                    spi_cs_n<=1;spi_sck<=0;
                    if(req && !seen)begin
                        seen<=1;
                        if(!byte_access && address[0])begin ready<=1;error<=1;end
                        else state<=write?WREN:CMD;
                    end
                end
                GAP:begin spi_cs_n<=1;state<=CMD;end
                DONE:begin spi_cs_n<=1;ready<=1;state<=IDLE;end
                default:begin
                    spi_cs_n<=0;spi_sck<=0;spi_mosi<=next_byte[7];
                    tx<=next_byte[6:0];bit_count<=0;divider<=0;active<=1;
                end
            endcase
        end
    end
endmodule
