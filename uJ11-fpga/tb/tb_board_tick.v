`timescale 1ns/1ps
module tb_board_tick;
    parameter integer DIVISOR=591200;
    reg clk=0,reset=1;wire terminal;
    integer i,events=0;
    always #5 clk=~clk;
    uj11_tick #(.DIVISOR(DIVISOR)) dut(clk,reset,terminal);
    initial begin
        repeat(5)@(negedge clk);reset=0;
        for(i=1;i<=2*DIVISOR;i=i+1)begin
            @(posedge clk);
            if(terminal !== (i%DIVISOR==0))$fatal(1,"tick phase error at%0d divisor%0d",i,DIVISOR);
            if(terminal)events=events+1;
            @(negedge clk);
        end
        reset=1;repeat(3)@(negedge clk);reset=0;
        for(i=1;i<=DIVISOR;i=i+1)begin
            @(posedge clk);
            if(terminal !== (i==DIVISOR))$fatal(1,"reset phase error");
            @(negedge clk);
        end
        $display("PASS exact KW11 period/reset: divisor%0d, 3 periods",DIVISOR);$finish;
    end
endmodule
