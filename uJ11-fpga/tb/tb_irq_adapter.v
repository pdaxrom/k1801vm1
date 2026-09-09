`timescale 1ns/1ps
module tb_irq_adapter;
    reg clk=0,reset=1,event_irq=0,uart_irq=0,irq_ack=0;
    reg [8:1] uart_vector=8'o030;
    wire irq_valid,uart_ack,timer_pending;wire [2:0] irq_priority;wire [8:1] irq_vector;
    integer c=0,i;
    uj11_irq_lsi11 dut(.*);
    always #5 clk=~clk;
    task tick;begin @(posedge clk);#1;end endtask
    task check;
        input valid,pending,ack;input [2:0] priority_value;input [8:1] vector_value;
        begin #1;c=c+1;if({irq_valid,timer_pending,uart_ack,irq_priority,irq_vector}!==
            {valid,pending,ack,priority_value,vector_value})$fatal(1,"IRQ adapter check %0d",c);end
    endtask
    initial begin
        tick;@(negedge clk);reset=0;check(0,0,0,4,8'o030);
        uart_irq=1;check(1,0,0,4,8'o030);
        event_irq=1;tick;@(negedge clk);event_irq=0;check(1,1,0,6,8'o040);
        repeat(500)begin tick;check(1,1,0,6,8'o040);end
        @(negedge clk);irq_ack=1;event_irq=1;tick;check(1,1,0,6,8'o040); // set wins
        @(negedge clk);event_irq=0;tick;check(1,0,1,4,8'o030);
        @(negedge clk);uart_irq=0;irq_ack=0;check(0,0,0,4,8'o030);
        for(i=0;i<256;i=i+1)begin
            uart_vector=i[7:0];uart_irq=1;irq_ack=1;check(1,0,1,4,i[7:0]);
        end
        event_irq=1;tick;@(negedge clk);reset=1;tick;check(1,0,1,4,8'hff);
        $display("PASS IRQ adapter: %0d checks; pulse retention, timer priority, simultaneous tick/ACK, UART vectors, reset",c);$finish;
    end
endmodule
