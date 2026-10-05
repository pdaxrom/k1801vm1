`timescale 1ns/1ps
module tb_clock_mailbox;
    reg source_clk = 0, target_clk = 0, reset = 1, idle = 1;
    always #17 source_clk = !source_clk;
    always #10 target_clk = !target_clk;
    reg [2:0] requested = 0;
    wire [2:0] target, applied, confirmed;
    wire cpu_p, cpu_n;
    bk_clock_mailbox mailbox(source_clk,target_clk,reset,requested,target,applied,confirmed);
    bk_cpu_clock clock_generator(target_clk,reset,target,idle,cpu_p,cpu_n,applied,1'b0);
    integer from_mode, to_mode;
    task select(input [2:0] mode);
        begin
            @(negedge source_clk); requested = mode;
            wait(confirmed == mode && applied == mode && mailbox.ack_sync[1] == mailbox.toggle);
            repeat(8) @(negedge source_clk);
        end
    endtask
    initial begin
        repeat(8) @(negedge source_clk); reset = 0;
        // BIOS selection is arbitrary, including all nonadjacent transitions.
        for(from_mode=0;from_mode<7;from_mode=from_mode+1) begin
            for(to_mode=0;to_mode<7;to_mode=to_mode+1) begin
                select(from_mode[2:0]);
                @(negedge target_clk); idle = 0;
                @(negedge source_clk); requested = to_mode[2:0];
                repeat(24) @(negedge source_clk);
                if(applied != from_mode || confirmed != from_mode) $fatal(1,"busy clock changed");
                @(negedge target_clk); idle = 1;
                wait(confirmed == to_mode && applied == to_mode);
            end
        end
        // Requests arriving while a previous payload is held must be delivered.
        idle = 0; requested = 1;
        repeat(12) @(negedge source_clk);
        requested = 5;
        repeat(12) @(negedge source_clk);
        idle = 1; wait(confirmed == 5 && applied == 5);
        $display("PASS clock mailbox: all 49 transitions, idle barrier, queued replacement");
        $finish;
    end
    initial begin #1000000; $fatal(1,"clock mailbox timeout"); end
endmodule
