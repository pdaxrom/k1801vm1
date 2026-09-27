`timescale 1ns/1ps
module tb_hc7000_hg;
    reg clk=0, reset=1, oe=0;
    reg [3:0] rows=0;
    wire [3:0] filtered;
    always #20.833 clk=~clk;
    uj11_hg_inputs dut(clk,reset,oe,rows,filtered);
    initial begin
        repeat(4)@(negedge clk);reset=0;rows=7;
        repeat(4)@(negedge clk);
        if(filtered!==0)$fatal(1,"inactive JTAG pins became keys");
        oe=1;repeat(4)@(negedge clk);
        if(filtered!==7)$fatal(1,"HG inputs missing");
        oe=0;repeat(20)@(negedge clk);
        if(filtered!==7)$fatal(1,"SELECT hidden before host released previous block");
        rows=0;repeat(5)@(negedge clk);
        if(filtered!==0)$fatal(1,"host release not observed");
        // A new block can request service after seeing the real falling SELECT.
        oe=1;rows=5;repeat(5)@(negedge clk);
        if(filtered!==5)$fatal(1,"second block inputs missing");
        reset=1;oe=0;repeat(4)@(negedge clk);
        if(filtered!==0)$fatal(1,"reset did not mask inputs");
        $display("PASS HC7000 HG: inactive pins, multi-block SELECT handoff, reset");$finish;
    end
endmodule
