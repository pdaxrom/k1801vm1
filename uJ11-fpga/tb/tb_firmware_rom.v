`timescale 1ns/1ps
module tb_firmware_rom;
    reg clk=0,enable=0;reg [8:0] address=0;wire [15:0] data;
    reg [1:0] write_enable=0;
    reg [15:0] write_data=0;
    reg [15:0] expected[0:511];integer i,j,k,checks=0;
    always #5 clk=~clk;
    uj11_firmware_rom dut(clk,enable,address,data,write_enable,write_data);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task read_check(input [8:0] a);
        begin
            @(negedge clk);enable=1;write_enable=0;address=a;
            @(posedge clk);#1;
            if(data!==expected[a])$fatal(1,"firmware/store word%0d got%h want%h",a,data,expected[a]);
            checks=checks+1;
            @(negedge clk);enable=0;address=~a;write_enable=3;write_data=~data;
            @(posedge clk);#1;
            if(data!==expected[a])$fatal(1,"firmware enable hold%0d",a);
        end
    endtask
    task write_check(input [8:0] a,input [1:0] mask,input [15:0] datum);
        begin
            @(negedge clk);enable=1;write_enable=mask;write_data=datum;address=a;
            @(posedge clk);#1;
            if(a>=240 && a<256)begin
                if(mask[0])expected[a][7:0]=datum[7:0];
                if(mask[1])expected[a][15:8]=datum[15:8];
            end
            read_check(a);
        end
    endtask
    initial begin
        $readmemh("microcode/generated/firmware.mem",expected);#20;
        for(i=0;i<512;i=i+1)read_check(i[8:0]);
        // Every address: even an asserted write must not corrupt firmware.
        for(i=0;i<512;i=i+1)write_check(i[8:0],3,~expected[i]);
        for(i=240;i<256;i=i+1)
            for(j=0;j<4;j=j+1)
                for(k=0;k<256;k=k+1)write_check(i[8:0],j[1:0],{k[7:0],~k[7:0]});
        for(i=0;i<512;i=i+1)read_check(i[8:0]);
        $display("PASS firmware/RK EBR: %0d reads/holds; both byte lanes; ROM write protection; all 512 words",checks);$finish;
    end
endmodule
