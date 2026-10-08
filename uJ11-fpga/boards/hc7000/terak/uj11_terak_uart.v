`timescale 1ns/1ps
// Serial framing/IRQ logic adapted from rtl/peripherals/wbc_uart_xo2.v,
// derived from cpu11 xen/lib/wbc_uart.v, Copyright (c) 2014-2019 by 1801BM1@gmail.com.
// TERAK 8512 asynchronous unit, fixed switch configuration 8 data / no parity.
// One shared 16x baud divider keeps this channel within the HC7000 LUT budget.
module uj11_terak_uart #(parameter CLOCK_HZ=50000000,BAUD=19200)(
    input wire clk,reset,request,writing,
    input wire [2:0] address,
    input wire [15:0] write_data,
    output wire [15:0] read_data,
    input wire rx,
    output wire tx,
    output reg rx_irq,tx_irq,
    input wire rx_ack,tx_ack
);
    localparam DIVISOR=(CLOCK_HZ+BAUD*8)/(BAUD*16);
    localparam WIDTH=$clog2(DIVISOR);
    reg [WIDTH-1:0] divider;
    wire tick=divider==0;
    reg [1:0] rx_sync;
    reg rx_ie,tx_ie,rx_full,overrun,framing,tx_break;
    reg [7:0] rx_shift,rx_buffer;
    reg [8:0] tx_shift;
    reg [3:0] rx_bits,tx_bits,rx_phase,tx_phase;
    reg rx_condition_seen,tx_condition_seen;
    wire low_write=request && writing && !address[0];
    wire rx_read=request && !writing && address[2:1]==1;
    wire rx_condition=rx_full && rx_ie;
    wire tx_done=tx_bits==0;
    wire tx_condition=tx_done && tx_ie;
    assign tx=(tx_done || tx_shift[0]) && !tx_break;
    // MODEM READY is tied active: the HC7000 exposes RX/TX without modem pins.
    // BREAK control is write-only; ERR is the OR of receiver error flags.
    assign read_data=address[2:1]==0 ?
        {overrun | framing,1'b0,framing,overrun,4'b0,rx_full,rx_ie,6'b0} :
        address[2:1]==1 ? {8'b0,rx_buffer} :
        address[2:1]==2 ? {1'b1,7'b0,tx_done,tx_ie,6'b0} : 16'b0;
    always @(posedge clk)begin
        if(reset)begin divider<=DIVISOR-1;rx_sync<=3;end
        else begin
            divider<=tick ? DIVISOR-1 : divider-1'b1;
            rx_sync<={rx_sync[0],rx};
        end
    end
    always @(posedge clk)begin
        if(reset)begin
            tx_ie<=0;tx_break<=0;tx_bits<=0;tx_phase<=0;tx_shift<=9'h1ff;
        end else begin
            if(low_write && address[2:1]==2)begin tx_ie<=write_data[6];tx_break<=write_data[0];end
            if(low_write && address[2:1]==3 && tx_done)begin
                tx_shift<={write_data[7:0],1'b0};tx_bits<=10;tx_phase<=15;
            end else if(tick && !tx_done)begin
                tx_phase<=tx_phase-1'b1;
                if(tx_phase==0)begin tx_bits<=tx_bits-1'b1;tx_shift<={1'b1,tx_shift[8:1]};end
            end
        end
    end
    always @(posedge clk)begin
        if(reset)begin
            rx_ie<=0;rx_full<=0;overrun<=0;framing<=0;
            rx_bits<=0;rx_phase<=0;rx_shift<=0;rx_buffer<=0;
        end else begin
            if(low_write && address[2:1]==0)rx_ie<=write_data[6];
            if(rx_read)begin rx_full<=0;overrun<=0;framing<=0;end
            // A low stop bit can be a continuous BREAK. Wait for mark before
            // accepting another start, rather than receiving repeated zeros.
            if(rx_bits==11)begin
                if(rx_sync[1])rx_bits<=0;
            end else if(rx_bits==0)begin
                if(!rx_sync[1])begin rx_bits<=1;rx_phase<=7;end
            end else if(tick)begin
                rx_phase<=rx_phase-1'b1;
                if(rx_phase==0)begin
                    if(rx_bits==1 && rx_sync[1])rx_bits<=0;
                    else if(rx_bits==10)begin
                        rx_bits<=rx_sync[1] ? 0 : 11;rx_buffer<=rx_shift;rx_full<=1;
                        overrun<=rx_full && !rx_read;framing<=!rx_sync[1];
                    end else begin
                        rx_bits<=rx_bits+1'b1;
                        if(rx_bits!=1)rx_shift<={rx_sync[1],rx_shift[7:1]};
                    end
                end
            end
        end
    end
    // Vector ACK clears the latch; the DONE/IE condition must cycle to rearm.
    always @(posedge clk)begin
        if(reset)begin rx_irq<=0;tx_irq<=0;rx_condition_seen<=0;tx_condition_seen<=0;end
        else begin
            rx_condition_seen<=rx_condition;tx_condition_seen<=tx_condition;
            if(rx_ack || rx_read || !rx_condition)rx_irq<=0;
            else if(!rx_condition_seen)rx_irq<=1;
            if(tx_ack || !tx_condition)tx_irq<=0;
            else if(!tx_condition_seen)tx_irq<=1;
        end
    end
endmodule
