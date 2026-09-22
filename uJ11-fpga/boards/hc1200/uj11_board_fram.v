// Production 29.56 MHz SPI FRAM transport with a sequential READ cursor.
`timescale 1ns/1ps
// Board-only held-request transport, SPI mode 0. Address/data/control must
// remain stable through ready; the uJ11 engine provides that contract.
// Bank is a private peripheral-storage bit, not CPU address translation.
module uj11_board_fram #(parameter integer CLK_DIV=1)(
    input wire clk,rst,req,write,byte_access,bank,
    input wire keep_read,close_read,
    input wire [15:0] address,wdata,
    output reg [15:0] rdata,
    output reg ready,error,
    output wire busy,
    output reg spi_cs_n,
    output wire spi_sck,
    output reg spi_mosi,
    input wire spi_miso
);
    localparam [3:0] IDLE=0,WREN=1,GAP=2,CMD=3,BANK=4,
                     HIGH=5,LOW=6,DATA_LO=7,DATA_HI=8,DONE=9;
    reg [3:0] state;
    reg active,seen;
    // Only demand reads: no cached data or clocks before a real request.
    // CS itself records whether the FRAM read cursor is still valid.
    reg [14:0] next_word;
    wire [14:0] advanced_word;
    assign advanced_word[0]=!address[1];
    genvar increment_bit;
    generate for(increment_bit=1;increment_bit<15;increment_bit=increment_bit+1)begin:increment_bits
        assign advanced_word[increment_bit]=address[increment_bit+1] ^ (&address[increment_bit:1]);
    end endgenerate

    wire retain=keep_read && !close_read && !write && !byte_access &&
        !bank && !address[0] && !(&address[15:1]);
    wire resume_read=retain && !spi_cs_n && address[15:1]==next_word;
    reg [6:0] tx;

    reg [6:0] rx;

    reg [2:0] bit_count;
    reg [7:0] next_byte;
    assign busy=state!=IDLE;
    // ODDRXE samples D1 on posedge, emits it on negedge, and drives D0=0
    // on the following posedge. MISO is captured before that falling SCK
    // reaches the FRAM. MOSI changes on posedge, half a cycle before SCK rises.
    // At bit_count=7 the final pulse is already on the wire; stop the NEXT one.
    wire serial_state=state!=IDLE && state!=GAP && state!=DONE;
    wire clock_pulse=!rst && serial_state && (!active || bit_count!=7);
    // Reset is synchronous like CS/control; masking D1 finishes the current
    // high half-period, rather than asynchronously truncating an SCK pulse.
    ODDRXE clock_output(.SCLK(clk),.RST(1'b0),.D0(1'b0),.D1(clock_pulse),.Q(spi_sck));
    // synthesis translate_off
    initial if(CLK_DIV!=1)$fatal(1,"CP56 requires CLK_DIV=1");
    // synthesis translate_on
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
            next_word<=0;state<=IDLE;active<=0;seen<=0;rdata<=0;ready<=0;error<=0;
            spi_cs_n<=1;spi_mosi<=0;

            tx<=0;rx<=0;bit_count<=0;

        end else begin
            ready<=0;error<=0;
            if(!req)seen<=0;
            if(active)begin
                rx<={rx[5:0],spi_miso};
                if(bit_count==7)begin
                    active<=0;
                    if(state==DATA_LO)begin
                        rdata[7:0]<={rx[6:0],spi_miso};
                        if(byte_access)rdata[15:8]<=0;
                    end
                    if(state==DATA_HI)rdata[15:8]<={rx[6:0],spi_miso};
                    state<=(state==DATA_LO && byte_access)?DONE:state+1'b1;
                end else begin
                    bit_count<=bit_count+1'b1;
                    spi_mosi<=tx[6];tx<={tx[5:0],1'b0};
                end
            end else case(state)
                IDLE:begin
                    if(close_read)spi_cs_n<=1;

                    if(req && !seen)begin
                        seen<=1;
                        // A miss raises CS for a complete system clock before
                        // CMD/WREN starts. At 29.56 MHz this exceeds tSHSL.
                        spi_cs_n<=!resume_read;
                        if(!byte_access && address[0])begin ready<=1;error<=1;end
                        else state<=write?WREN:(resume_read?DATA_LO:CMD);
                    end
                end
                GAP:begin spi_cs_n<=1;state<=CMD;end
                DONE:begin
                    spi_cs_n<=!retain;ready<=1;state<=IDLE;
                    next_word<=advanced_word;
                end
                default:begin
                    spi_cs_n<=0;spi_mosi<=next_byte[7];
                    tx<=next_byte[6:0];bit_count<=0;active<=1;
                end
            endcase
        end
    end
endmodule
