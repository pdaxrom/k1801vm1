`timescale 1ns/1ps
module tb_serv_sram;
    reg clk=0,reset=1,cycle=0,write=0;
    always #5 clk=~clk;
    reg [20:0] address;
    reg [31:0] data;
    reg [3:0] lanes;
    wire acknowledged,request,writing;
    wire [31:0] result;wire [21:0] physical;
    wire [15:0] output_data;wire [1:0] output_lanes;
    reg ready=0;reg [15:0] input_data;
    uj11_serv_sram dut(.*);
    reg [15:0] memory[0:31];
    integer latency=0,transactions=0;
    always @(posedge clk)begin
        if(!request)begin ready<=0;latency<=0;end
        else if(!ready)begin
            latency<=latency+1;
            if(latency==3)begin
                if(physical<'h1e4000 || physical>='h200000)$fatal(1,"SERV reservation");
                input_data<=memory[physical[5:1]];
                if(writing)begin
                    if(output_lanes[0])memory[physical[5:1]][7:0]<=output_data[7:0];
                    if(output_lanes[1])memory[physical[5:1]][15:8]<=output_data[15:8];
                end
                ready<=1;transactions++;
            end
        end
    end
    task access(input bit w,input [20:0] a,input [31:0] d,input [3:0] s);
        @(negedge clk);cycle=1;write=w;address=a;data=d;lanes=s;
        wait(acknowledged);repeat(4)@(negedge clk);
        cycle=0;repeat(4)@(negedge clk);
    endtask
    initial begin
        for(integer i=0;i<32;i++)memory[i]=16'hdead;
        repeat(3)@(negedge clk);reset=0;
        access(1,'h1e4000,32'h12345678,4'b1111);
        access(0,'h1e4000,0,4'b1111);
        if(result!=32'h12345678)$fatal(1,"32-bit split access");
        access(1,'h1e4000,32'h00ab0000,4'b0100);
        access(0,'h1e4000,0,4'b1111);
        if(result!=32'h12ab5678)$fatal(1,"high-half byte lane");
        access(1,'h1e4000,32'h0000cd00,4'b0010);
        access(0,'h1e4000,0,4'b1111);
        if(result!=32'h12abcd78)$fatal(1,"low-half byte lane");
        access(1,'h1e3ffc,32'hbadcafe,4'b1111);
        if(transactions!=10 || result!=0)$fatal(1,"out-of-reservation access");
        $display("PASS SERV SRAM: split words, byte lanes, held acknowledgements and reservation");$finish;
    end
    initial begin #100000;$fatal(1,"SERV SRAM timeout");end
endmodule
