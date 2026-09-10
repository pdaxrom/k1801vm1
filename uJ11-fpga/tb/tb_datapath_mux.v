`timescale 1ns/1ps
// Four-state comparison, including cold/uninitialized RF and PASS D writes.
module tb_datapath_mux;
    reg clk=0,reset=1,enable=0,carry=0,byte_mode=0;
    reg [3:0] a=0,b=0,operation=0;
    reg [2:0] pair=0,destination=0;
    reg [15:0] d=0;
    wire [15:0] read_a,read_b,result,q,writeback;
    wire [15:0] old_a,old_b,old_result,old_q,old_writeback;
    wire [3:0] nzvc,old_flags;
    wire rf_write,old_write;
    integer i,n,checks=0;
    reg [31:0] seed=32'h6c72ad51;
    uj11_datapath dut(.*);
    uj11_datapath_reference gold(.clk(clk),.reset(reset),.enable(enable),.a(a),.b(b),
        .operation(operation),.pair(pair),.destination(destination),.d(d),.carry(carry),.byte_mode(byte_mode),
        .read_a(old_a),.read_b(old_b),.result(old_result),.q(old_q),.nzvc(old_flags),
        .rf_write(old_write),.writeback(old_writeback));
    task compare;
        begin
            if({read_a,read_b,result,q,nzvc,rf_write,writeback} !==
               {old_a,old_b,old_result,old_q,old_flags,old_write,old_writeback})
                $fatal(1,"datapath outputs case%0d pair%0d dest%0d op%0d",checks,pair,destination,operation);
            for(i=0;i<16;i=i+1)if(dut.rf.words[i]!==gold.rf.words[i])
                $fatal(1,"datapath RF state case%0d R%0d",checks,i);
        end
    endtask
    task tick;
        begin
            #1;compare();clk=1;#1;compare();clk=0;checks=checks+1;
        end
    endtask
    initial begin
        tick();reset=0;
        // All controls with initially unknown RF, without changing state.
        for(n=0;n<4096;n=n+1)begin
            {carry,byte_mode,operation,destination,pair}=n[11:0];d=16'h96a5;tick();
        end
        // Architectural initialization through the real single RF write port.
        enable=1;pair=6;destination=1;operation=0;byte_mode=0;
        for(n=0;n<16;n=n+1)begin b=n[3:0];d=16'h5a63 ^ (16'(n)*16'h1367);tick();end
        // Every control combination, varied live RF/Q/operand data and holds.
        for(n=0;n<262144;n=n+1)begin
            seed=seed^(seed<<13);seed=seed^(seed>>17);seed=seed^(seed<<5);
            {carry,byte_mode,operation,destination,pair}=n[11:0];
            a=seed[3:0];b=seed[7:4];d=seed[31:16];enable=seed[8];reset=seed[15:9]==0;
            tick();
        end
        $display("PASS four-state datapath: %0d cycles, all controls, unknown RF/PASS init, RF/Q/hold/reset",checks);
        $finish;
    end
endmodule
