`timescale 1ns/1ps
module tb_terak_console;
    reg clk=0,reset=1,bus_reset=0,request=0,writing=0,key_valid=0,ack=0;
    always #10 clk=~clk;
    reg [12:0] address=0;
    reg [15:0] wd=0;
    reg [7:0] key_data=0;
    wire [15:0] rd,gar,vir,vcr,vector;
    wire irq,key_ready;
    uj11_terak_console console(.clk(clk),.reset(reset),.bus_reset(bus_reset),.request(request),.writing(writing),
        .address(address),.lanes(2'b11),.write_data(wd),.read_data(rd),.gar(gar),.vir(vir),.vcr(vcr),
        .key_valid(key_valid),.key_data(key_data),.key_ready(key_ready),.irq(irq),.vector(vector),.irq_ack(ack),.audio());
    reg [15:0] result;
    task access(input [12:0] a,input bit w,input [15:0] data);
        @(negedge clk);request=1;address=a;writing=w;wd=data;
        #1;result=rd;
        @(negedge clk);request=0;@(negedge clk);
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        access(13'o17564,0,0);if(result!=16'o4200)$fatal(1,"ESR line-clock latency indication");
        access(13'o17566,1,16'hff41);
        if(!irq || vector!=16'o164)$fatal(1,"unconditional emulator IRQ");
        access(13'o17746,0,0);if(result!=16'hff41 || !irq || console.output_done)$fatal(1,"16-bit EDBA/side effect");
        access(13'o17564,1,16'h40);access(13'o17566,0,0);
        if(result!=16'hff41 || !console.output_done || irq)$fatal(1,"EDB read acknowledge");
        repeat(23990)@(negedge clk);if(irq)$fatal(1,"printer IRQ early");
        repeat(20)@(negedge clk);if(!irq || vector!=16'o64)$fatal(1,"480 us IRQ missing");
        ack=1;@(negedge clk);ack=0;
        access(13'o17560,1,16'h40);key_data=8'h78;key_valid=1;@(negedge clk);key_valid=0;
        if(!irq || vector!=16'o60 || key_ready)$fatal(1,"keyboard latch/IRQ");
        access(13'o17562,0,0);if(result!=120 || irq || !key_ready)$fatal(1,"KDR acknowledge");
        access(13'o17740,1,16'o20000);access(13'o17742,1,16'h1809);access(13'o17744,1,16'o377);
        if(gar!=16'o20000 || vir!=16'h1809 || vcr!=16'o377)$fatal(1,"video registers");
        bus_reset=1;@(negedge clk);bus_reset=0;
        if(irq || vcr || vir || gar || !key_ready)$fatal(1,"RESET");
        $display("PASS native console latch, EDBA, 480 us IRQ, keyboard, video CSRs, RESET");$finish;
    end
    initial begin #2000000;$fatal(1,"console timeout");end
endmodule
