`timescale 1ns/1ps
module tb_rom;
    reg clk=0, enable=0;
    reg [9:0] address=0;
    wire [35:0] data;
    reg [35:0] expected[0:1023];
    reg [35:0] held;
    integer i;
    uj11_rom dut(.*);
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    initial begin
        $readmemh("microcode/generated/checkpoint_seq.mem",expected);
        // Allow vendor PUR startup window to expire.
        #100;
        for(i=0;i<1024;i=i+1) begin
            @(negedge clk); enable=1; address=i[9:0];
            @(posedge clk); #1;
            if(data!==expected[i]) $fatal(1,"ROM[%h]=%h expected=%h",i,data,expected[i]);
            held=data;
            @(negedge clk); enable=0; address=~i[9:0];
            @(posedge clk); #1;
            if(data!==held) $fatal(1,"ROM enable did not hold");
        end
        $display("PASS ROM: all 1024 words and 1024 enable holds");
        $finish;
    end
endmodule
