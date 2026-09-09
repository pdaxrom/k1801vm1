`timescale 1ns/1ps
module tb_microseq;
    reg clk=0, reset=1, enable=0;
    reg [35:0] uword=0;
    reg [15:0] ir=0;
    reg [3:0] nzvc=0, selected_a=0;
    reg [9:0] dispatch_address=0;
    reg address_odd=0, byte_instruction=0, q0=0, loop_zero=0, bus_error=0, a_one=0, irq_pending=0, fault_redirect=0, fault_repair=0, trace_pending=0;
    wire [9:0] upc, next_address;
    integer checks=0, a, b, c, p;
    reg expected_condition;
    uj11_microseq dut(.*);
    always #5 clk=~clk;
    function [35:0] control;
        input [3:0] cmd, cond;
        input [9:0] target;
        begin control = {1'b1, cmd, 10'b0, target, cond, 7'b0}; end
    endfunction
    task check;
        input [9:0] expected;
        begin
            #1;
            checks=checks+1;
            if (next_address !== expected)
                $fatal(1,"seq upc=%h word=%h next=%h expected=%h",upc,uword,next_address,expected);
        end
    endtask
    task commit;
        begin
            @(negedge clk); enable=1;
            @(posedge clk); #1; enable=0;
        end
    endtask
    task jump;
        input [9:0] target;
        begin uword=control(0,0,target); commit; end
    endtask
    initial begin
        @(posedge clk); #1; reset=0;
        jump(10'h2fe);
        uword=0; check(10'h2ff);
        uword=36'h00000015a; check(10'h25a); // ALU PAGE preserves high bits
        uword=36'h000000200; check(10'h020);
        uword=36'h000000300; a_one=0; check(10'h2ff);
        a_one=1; check(10'h020);
        // Conditional retirement remains a combinational next-address choice
        // during a stall; the micro-PC and link do not change without enable.
        repeat(3)begin @(posedge clk);#1;if(upc!==10'h2fe)$fatal(1,"FETCH_A1 stall advanced");end
        a_one=0;
        jump(10'h3ff); uword=0; check(0); // 10-bit wrap
        jump(10'h055);
        for(c=0;c<16;c=c+1) begin
            for(a=0;a<16;a=a+1) begin
                for(p=0;p<8;p=p+1) begin
                    nzvc=a[3:0]; {bus_error,loop_zero,q0}=p[2:0];
                    case(c & 7)
                        0: expected_condition=1;
                        1: expected_condition=nzvc[0];
                        2: expected_condition=nzvc[1];
                        3: expected_condition=nzvc[2];
                        4: expected_condition=nzvc[3];
                        5: expected_condition=q0;
                        6: expected_condition=loop_zero;
                        7: expected_condition=bus_error;
                    endcase
                    if(c>=8) expected_condition=~expected_condition;
                    uword=control(1,c[3:0],10'h2a5);
                    check(expected_condition ? 10'h2a5 : 10'h056);
                end
            end
        end
        for(a=0;a<8;a=a+1) begin
            for(b=0;b<8;b=b+1) begin
                ir=0; ir[11:9]=a[2:0]; ir[5:3]=b[2:0];
                uword=control(5,0,10'h100); check(10'h100 | a[9:0]);
                uword=control(6,0,10'h208); check(10'h208 | b[9:0]);
                uword=control(7,0,10'h300); check(10'h300 | ((b==0) ? 10'd2 : 10'd0) |
                                                               ((a==0) ? 10'd1 : 10'd0));
            end
        end
        for(a=0;a<4;a=a+1) begin
            {address_odd,byte_instruction}=a[1:0];
            uword=control(8,0,10'h104); check(10'h104 | a[9:0]);
        end
        for(a=0;a<16;a=a+1) begin
            selected_a=a[3:0]; uword=control(9,0,10'h202);
            check(10'h202 | (((a&6)==6) ? 10'd0 : 10'd1));
        end
        for(a=0;a<1024;a=a+1) begin
            dispatch_address=a[9:0];
            uword=control(4,0,0); check(a[9:0]);
            uword=control(2,0,0); check(a[9:0]);
        end
        // Memory continuation and stop do not create a second sequencer FSM.
        uword=control(11,0,10'h155); check(10'h155);
        uword=control(12,0,10'h156); check(10'h156);
        uword=control(13,0,0); check(10'h055);
        uword=control(14,0,0); check(10'h055);
        irq_pending=1;check(10'h013);
        uword=36'h200;check(10'h013);
        uword=36'h300;a_one=0;check(10'h056);a_one=1;check(10'h013);
        trace_pending=1;check(10'h024);
        uword=control(14,0,0);check(10'h024);
        uword=36'h200;check(10'h024);
        uword=36'h300;a_one=0;check(10'h056);a_one=1;check(10'h024);
        trace_pending=0;
        irq_pending=0;fault_redirect=0;a_one=0;
        uword=control(15,0,10'h3d6); check(10'h3d6);
        // CALL wait must not push; RETURN wait must not pop.
        jump(10'h3ff); uword=control(10,0,10'h100);
        repeat(4) begin @(posedge clk); #1; if(upc!==10'h3ff || dut.link_valid) $fatal(1,"stall CALL"); end
        commit; if(upc!==10'h100) $fatal(1,"CALL target");
        uword=control(3,0,0); check(0);
        repeat(3) begin @(posedge clk); #1; if(!dut.link_valid || upc!==10'h100) $fatal(1,"stall RETURN"); end
        commit; if(upc!==0) $fatal(1,"RETURN wrap");
        check(10'h3ff); // underflow
        jump(10'h010); uword=control(10,0,10'h200); commit;
        uword=control(10,0,10'h300); check(10'h3ff); // nested CALL rejected
        uword=control(3,0,0); check(10'h011); commit;
        // A memory fault unwinds an EA CALL and wins over IRQ/ordinary dispatch.
        jump(10'h010); uword=control(10,0,10'h200); commit;
        fault_redirect=1; irq_pending=1; uword=control(2,0,0); check(10'h015);
        repeat(3)begin @(posedge clk);#1;if(!dut.link_valid || upc!==10'h200)$fatal(1,"fault stall");end
        commit; if(upc!==10'h015 || dut.link_valid)$fatal(1,"fault redirect/link");
        fault_redirect=0;irq_pending=0;
        uword=control(10,0,10'h200);commit;
        if(upc!==10'h200 || !dut.link_valid)$fatal(1,"handler CALL after fault");
        fault_redirect=1;uword=control(11,0,10'h225)|36'd4;check(10'h225);commit;
        fault_redirect=0;fault_repair=1;uword=0;check(10'h015);commit;
        if(upc!==10'h015 || dut.link_valid)$fatal(1,"fault repair continuation");
        fault_redirect=1;
        @(negedge clk); reset=1; check(0);
        @(posedge clk); #1;
        if(upc!==0 || dut.link_valid!==0) $fatal(1,"reset");
        $display("PASS microseq: %0d combinational checks, CALL/RETURN stalls and reset",checks);
        $finish;
    end
    initial begin #200000; $fatal(1,"microseq timeout"); end
endmodule
