`timescale 1ns/1ps
module tb_board_irq;
    parameter integer ROM_DECODE=1;
    reg clk=0,reset=1,valid=0;reg [2:0] priority_level=7;
    reg [15:0] vector=16'o100;
    wire irq_ack,waiting,request,reading,writing,byte_access,ack,stopped,retire;
    wire [15:0] address,data,rdata,psw;wire [9:0] upc;
    wire [31:0] beats,writes;
    integer count=0,cycles;
    always #5 clk=~clk;
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.irq_valid(valid),.irq_priority(priority_level),.irq_vector(vector[15:1]),
        .irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),.mem_addr(address),.mem_write_data(data),
        .mem_request(request),.mem_read(reading),.mem_write(writing),.mem_byte(byte_access),
        .mem_ack(ack),.mem_error(1'b0),.mem_read_data(rdata),.stopped(stopped),.fault_code(),
        .retire(retire),.debug_upc(upc),.debug_uword(),.ir(),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
    uj11_ram memory(clk,reset,request,reading,writing,byte_access,address,data,8'd3,ack,rdata,beats,writes);
    task poke(input [15:0] addr,value);
        begin memory.bytes[addr]=value[7:0];memory.bytes[addr+16'd1]=value[15:8];end
    endtask
    always @(posedge clk)if(!reset && irq_ack)begin
        if(vector!=16'o160000)$fatal(1,"ordinary interrupt bypassed IPL7");
        count<=count+1;valid<=0;
    end
    initial begin
        poke(0,1); // WAIT
        poke(2,1); // next WAIT, after private assist RTI
        poke(16'o160000,16'o400);poke(16'o160002,16'o340);
        poke(16'o400,2); // RTI
        repeat(5)@(negedge clk);reset=0;
        wait(upc==10'h020);@(negedge clk);dut.engine.dp.rf.words[6]=16'o2000;
        wait(waiting);@(negedge clk);valid=1;
        repeat(30)begin @(negedge clk);if(irq_ack || count!=0)$fatal(1,"IPL mask failed");end
        vector=16'o160000;
        cycles=0;
        while(count==0 || !waiting)begin
            @(negedge clk);cycles=cycles+1;
            if(stopped || cycles>200)$fatal(1,"private assist did not return");
        end
        if(count!=1 || dut.engine.dp.rf.words[6]!=16'o2000 || dut.engine.dp.rf.words[7]!=4 || psw!=16'o340)
            $fatal(1,"private assist damaged PC/SP/PSW");
        if({memory.bytes[16'o1775],memory.bytes[16'o1774]}!=2 ||
           {memory.bytes[16'o1777],memory.bytes[16'o1776]}!=16'o340)
            $fatal(1,"private assist stack frame");
        $display("PASS private IRQ: full 160000 vector, IPL7 assist, masked external IRQ, RTI frame; ROM_DECODE%0d",ROM_DECODE);$finish;
    end
    initial begin #100000;$fatal(1,"IRQ timeout");end
endmodule
