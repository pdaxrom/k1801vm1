`timescale 1ns/1ps
module tb_datapath;
    reg clk=0,reset=1,enable=0,carry=0;
    reg [3:0] a=0,b=0,operation=0;
    reg [2:0] pair=6,destination=0;
    reg [15:0] d=0;
    wire [15:0] read_a,read_b,result,q,writeback;
    wire [3:0] nzvc;
    wire rf_write;
    integer i,j,p,checks=0;
    reg [15:0] lhs,rhs;
    reg [15:0] expected[0:15];
    wire byte_mode=1'b0;
    uj11_datapath dut(.*);
    always #5 clk=~clk;
    task commit;
        begin @(negedge clk); enable=1; @(posedge clk); #1; enable=0; end
    endtask
    initial begin
        @(posedge clk); #1; reset=0;
        for(i=0;i<16;i=i+1) begin
            b=i[3:0]; d=16'h1234 ^ (16'h1111*i); expected[i]=d; destination=1; commit;
        end
        destination=2;d=16'ha5a5;commit;
        destination=0;d=16'h3c5a;
        for(i=0;i<16;i=i+1) for(j=0;j<16;j=j+1) begin
            a=i[3:0]; b=j[3:0]; #1;
            if(read_a!==expected[i] || read_b!==expected[j]) $fatal(1,"RF dual read");
            for(p=0;p<8;p=p+1)begin
                pair=p[2:0];
                case(p)
                    0:begin lhs=expected[i];rhs=expected[j];end
                    1:begin lhs=expected[i];rhs=16'ha5a5;end
                    2:begin lhs=expected[i];rhs=d;end
                    3:begin lhs=d;rhs=expected[j];end
                    4:begin lhs=0;rhs=expected[j];end
                    5:begin lhs=d;rhs=16'ha5a5;end
                    6:begin lhs=d;rhs=expected[i];end
                    7:begin lhs=expected[j];rhs=expected[i];end
                endcase
                operation=0;#1;if(result!==lhs)$fatal(1,"pair%0d lhs",p);
                operation=1;#1;if(result!==rhs)$fatal(1,"pair%0d rhs",p);
                checks=checks+2;
            end
        end
        a=1; b=2; pair=0; operation=2; destination=1;
        expected[2]=expected[1]+expected[2]; commit;
        if(read_b!==expected[2]) $fatal(1,"single-cycle RF+ALU writeback");
        pair=7;operation=4;expected[2]=expected[2]-expected[1];commit;
        if(read_b!==expected[2] || read_a!==expected[1])$fatal(1,"single-cycle BA SUB");
        operation=9;expected[2]=expected[2]&~expected[1];commit;
        if(read_b!==expected[2] || read_a!==expected[1])$fatal(1,"single-cycle BA BIC");
        // Q load, both coupled shifts, disabled and reset writes.
        pair=6; operation=0; destination=2; d=16'ha5a5; commit;
        if(q!==16'ha5a5) $fatal(1,"Q load");
        destination=3; b=0; d=16'h8001; commit;
        if(q!==16'h4b4a || read_b!==16'h0003) $fatal(1,"RFQ left");
        destination=2; d=16'ha5a5; commit;
        destination=4; d=16'h8001; commit;
        if(q!==16'hd2d2 || read_b!==16'hc000) $fatal(1,"RFQ right");
        d=0;
        repeat(4) begin @(posedge clk); #1; if(q!==16'hd2d2 || read_b!==16'hc000) $fatal(1,"stall"); end
        @(negedge clk); reset=1; enable=1;
        @(posedge clk); #1;
        if(q!==0 || read_b!==16'hc000 || rf_write!==0) $fatal(1,"reset write guard");
        $display("PASS datapath: 16 RF words, 256 dual reads, %0d pair checks, BA SUB/BIC writeback, Q/shift/stall/reset",checks); $finish;
    end
endmodule
