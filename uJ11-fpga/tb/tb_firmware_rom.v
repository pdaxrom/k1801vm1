`timescale 1ns/1ps
module tb_firmware_rom;
    reg clk=0,enable=0;reg [8:0] address=0;wire [15:0] data;
    reg [15:0] expected[0:511];integer i;
    always #5 clk=~clk;
    uj11_firmware_rom dut(clk,enable,address,data);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    initial begin
        $readmemh("microcode/generated/firmware.mem",expected);#20;
        for(i=0;i<512;i=i+1)begin
            @(negedge clk);enable=1;address=i[8:0];@(posedge clk);#1;
            if(data!==expected[i])$fatal(1,"firmware word%0d got%h want%h",i,data,expected[i]);
            @(negedge clk);enable=0;address=~i[8:0];@(posedge clk);#1;
            if(data!==expected[i])$fatal(1,"firmware enable hold%0d",i);
        end
        $display("PASS firmware ROM: all 512 words and 512 enable holds");$finish;
    end
endmodule
