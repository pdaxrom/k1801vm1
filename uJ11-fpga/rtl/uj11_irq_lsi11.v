`timescale 1ns/1ps
// Synchronous KW11-L pulse + resolved KL11 level. RK service is separate.
// A pending bit coalesces timer ticks; a new tick wins over simultaneous ACK.
module uj11_irq_lsi11(
    input wire clk,reset,event_irq,uart_irq,
    input wire [8:1] uart_vector,
    input wire irq_ack,
    output wire irq_valid,
    output wire [2:0] irq_priority,
    output wire [8:1] irq_vector,
    output wire uart_ack,
    output reg timer_pending
);
    always @(posedge clk)begin
        if(reset)timer_pending<=0;
        else timer_pending<=event_irq || (timer_pending && !irq_ack);
    end
    assign irq_valid=timer_pending || uart_irq;
    assign irq_priority=timer_pending ? 3'd6 : 3'd4;
    assign irq_vector=timer_pending ? 8'o040 : uart_vector; // byte vector 0100
    assign uart_ack=irq_ack && !timer_pending && uart_irq;
endmodule
