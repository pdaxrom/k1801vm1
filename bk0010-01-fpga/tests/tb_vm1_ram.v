`timescale 1ns/1ps
// Compare the unchanged cpu11 register file with the two production EBR lanes.
module tb_vm1_ram;
    reg clk = 0;
    always #25 clk = !clk;
    GSR GSR_INST(1'b1);
    PUR PUR_INST(1'b1);
    reg [5:0] a = 0, b = 0;
    reg [1:0] lanes = 0;
    reg [15:0] da = 0, db = 0;
    reg wa = 0, wb = 0;
    wire [15:0] qa, qb, ra, rb;
    vm1_vcram actual(a,b,lanes,clk,da,db,wa,wb,qa,qb);
    vm1_vcram_original reference(a,b,lanes,clk,da,db,wa,wb,ra,rb);
    integer count;
    task check;
        begin
            @(posedge clk); #2;
            if(qa !== ra || qb !== rb)
                $fatal(1,"EBR mismatch A=%0d B=%0d WE=%b/%b lanes=%b actual=%h/%h reference=%h/%h",
                       a,b,wa,wb,lanes,qa,qb,ra,rb);
        end
    endtask
    initial begin
        #300;
        for(count=0;count<64;count=count+1) begin
            @(negedge clk); a = count; b = 63-count; check();
        end
        // Both clocks are the CPU negative edge; simultaneous same-address
        // writes are excluded because the CPU never writes through port B.
        for(count=0;count<10000;count=count+1) begin
            @(negedge clk);
            a = $random; b = $random; da = $random; db = $random;
            lanes = $random; wa = $random; wb = 0;
            check();
        end
        $display("PASS official Lattice VM1 EBR: 64 initialized words, 10000 dual-port reads/byte writes");
        $finish;
    end
endmodule
