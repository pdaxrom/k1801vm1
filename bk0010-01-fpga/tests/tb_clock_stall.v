`timescale 1ns/1ps
module tb_clock_stall;
    reg clk=0,reset=1,idle=1,stall=0;
    reg [2:0] requested=0;
    wire cp,cn;
    wire [2:0] active;
    always #10 clk=!clk;
    bk_cpu_clock dut(clk,reset,requested,idle,cp,cn,active,stall);
    integer mode,phase,edges=0,before_edges;
    time last_edge=0;
    always @(cp)begin
        if(!reset)begin
            if(last_edge!=0&&$time-last_edge<20)$fatal(1,"Short CPU phase on disk wait");
            last_edge=$time;edges=edges+1;
        end
    end
    initial begin
        repeat(8)@(negedge clk);reset=0;
        for(mode=0;mode<7;mode=mode+1)begin
            for(phase=0;phase<10;phase=phase+1)begin
                idle=1;requested=mode;wait(active==mode);repeat(20)@(negedge clk);
                #3;idle=0;#(phase*7+1);stall=1;
                wait(dut.stall_sync==3);wait(!cp);repeat(20)@(negedge clk);
                before_edges=edges;requested=(mode+1)%7;
                repeat(100)@(negedge clk);
                if(cp||edges!=before_edges||active!=mode)$fatal(1,"Disk wait changed clock/frequency");
                #7;stall=0;wait(cp);repeat(20)@(negedge clk);
                if(active!=mode)$fatal(1,"Busy frequency committed on disk reply");
                idle=1;wait(active==(mode+1)%7);
            end
        end
        $display("PASS disk clock wait: all 7 frequencies, 70 asynchronous phase offsets, queued frequency, minimum widths");
        $finish;
    end
    initial begin #1000000;$fatal(1,"Clock stall timeout");end
endmodule
