`timescale 1ns/1ps
// Exact-period KW11 timebase without a wide binary incrementer. The Galois
// recurrence x^20+x^3+1 visits all 2^20-1 nonzero states. Only the terminal
// state is needed; the guest never reads a binary counter value.
module uj11_tick #(parameter integer DIVISOR=591200)(
    input wire clk,reset,output wire terminal);
    function [19:0] multiply;
        input [19:0] a,b;
        reg [19:0] x,y,value;
        integer i;
        begin
            x=a;y=b;value=0;
            for(i=0;i<20;i=i+1)begin
                if(y[0])value=value^x;
                x={x[18:0],1'b0} ^ ({20{x[19]}} & 20'h00009);
                y=y>>1;
            end
            multiply=value;
        end
    endfunction
    function [19:0] terminal_state;
        input integer steps;
        reg [19:0] value,power;
        integer i;
        begin
            value=1;power=2;
            for(i=0;i<20;i=i+1)begin
                if(steps[i])value=multiply(value,power);
                power=multiply(power,power);
            end
            terminal_state=value;
        end
    endfunction
    localparam [19:0] LAST=terminal_state(DIVISOR-1);
    reg [19:0] state;
    assign terminal=state==LAST;
    always @(posedge clk)begin
        if(reset || terminal)state<=20'd1;
        else state<={state[18:0],1'b0} ^ ({20{state[19]}} & 20'h00009);
    end
`ifndef SYNTHESIS
    initial if(DIVISOR<1 || DIVISOR>1048575)$fatal(1,"KW11 divisor outside LFSR period");
`endif
endmodule
