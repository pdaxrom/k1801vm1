`timescale 1ns/1ps
module tb_decode_rom;
    reg clk=0,enable=0;
    reg [15:0] incoming=0,ir=0;
    wire [9:0] actual,expected;
    integer i;
    always #5 clk=~clk;
    always @(posedge clk)if(enable)ir<=incoming;
    uj11_decode_rom dut(clk,enable,incoming,actual);
    uj11_decode_gold gold(ir,expected);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    initial begin
        #20;
        for(i=0;i<65536;i=i+1)begin
            @(negedge clk);enable=1;incoming=i[15:0];
            @(posedge clk);#1;
            if(actual!==expected)$fatal(1,"opcode %o got %h expected %h",ir,actual,expected);
            @(negedge clk);enable=0;incoming=~i[15:0];
            @(posedge clk);#1;
            if(actual!==expected)$fatal(1,"disabled table changed for opcode %o",ir);
        end
        $display("PASS synchronous decode: all 65536 opcodes and 65536 enable holds match CP27");$finish;
    end
endmodule
