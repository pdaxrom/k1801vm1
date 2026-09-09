`timescale 1ns/1ps
module tb_prefetch_control;
    reg clk=0,reset=1;reg [35:0] uword=0;wire allow;
    uj11_prefetch_control dut(.*);
    always #5 clk=~clk;
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        tick;if(allow)$fatal(1,"reset permit");
        @(negedge clk);reset=0;uword[35]=1;uword[4]=0;
        #1;if(!allow)$fatal(1,"control permit");tick;
        @(negedge clk);uword=36'h0000000ff;
        #1;if(!allow)$fatal(1,"ALU immediate changed policy");tick;
        @(negedge clk);uword[35]=1;
        #1;if(allow)$fatal(1,"control hold");repeat(4)tick;
        @(negedge clk);uword=0;
        #1;if(allow)$fatal(1,"ALU did not inherit hold");tick;
        @(negedge clk);uword[35]=1;
        #1;if(!allow)$fatal(1,"resume");tick;
        @(negedge clk);uword=36'h000000300;
        #1;if(allow)$fatal(1,"FETCH_A1 launched speculation");tick;
        @(negedge clk);uword=36'h000000200;
        #1;if(!allow)$fatal(1,"FETCH_A1 changed inherited policy");tick;
        $display("PASS prefetch control: pause/resume, inheritance over ALU/immediate, stalls/reset");
        $finish;
    end
endmodule
