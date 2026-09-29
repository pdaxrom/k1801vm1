`timescale 1ns/1ps
module tb_iop_bus;
    reg dma_map_enabled=0;
    reg clk=0,reset=1,bus_reset=0,enabled=1,request=0,write=0,irq_ack=0,iop_write=0;
    always #5 clk=~clk;
    reg [12:0] address=0;reg [1:0] lanes=3;reg [15:0] wdata=0;
    reg [2:0] iop_address=0;reg [31:0] iop_data=0;
    wire [31:0] iop_rdata;wire [15:0] rdata;wire ready,error,irq,want_sd,dma_unibus,abort_dma,working;
    wire [8:0] vector;integer checks=0;
    uj11_mmu_iop_bus dut(.*,.owner(1'b1));
    task check(input bit yes,input string reason);if(!yes)$fatal(1,"%s",reason);checks++;endtask
    task put(input [2:0] a,input [31:0] v);
        @(negedge clk);iop_write=1;iop_address=a;iop_data=v;
        @(negedge clk);iop_write=0;
    endtask
    task get(input [2:0] a,input [31:0] v);
        @(negedge clk);iop_address=a;#1;check(iop_rdata===v,"SERV read");
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        request=1;address=13'o17401;write=1;lanes=2;wdata=16'hab00;
        get(0,9);get(1,{16'hab00,2'b10,1'b1,13'o17401});
        repeat(20)@(negedge clk);check(!ready,"CPU waits for SERV");
        put(2,16'h1234);check(ready && !error && rdata==16'h1234,"firmware response");
        get(0,8);dma_map_enabled=1;get(0,32'h10008);dma_map_enabled=0;put(2,17'h1ffff);check(rdata==16'h1234 && !error,"no second response for held request");
        request=0;@(negedge clk);request=1;put(2,17'h10000);check(ready && error,"firmware NXM");
        request=0;@(negedge clk);request=1;bus_reset=1;@(negedge clk);bus_reset=0;
        put(2,16'habcd);check(!ready && abort_dma,"reset rejects stale response and aborts DMA");
        put(0,2);put(2,16'h5678);check(ready && !error && rdata==16'h5678,"response after reset consumed");
        request=0;
        put(3,17'h100d4);check(irq && vector==9'o324,"generic vector");
        @(negedge clk);iop_write=1;iop_address=3;iop_data=17'h10080;irq_ack=1;
        @(negedge clk);iop_write=0;irq_ack=0;check(!irq && vector==9'o324,"ack preserves actual acknowledged vector");
        get(0,32'h350c);put(3,17'h10080);check(!irq,"publication cannot overwrite unconsumed ack");
        put(0,4);put(3,17'h10080);check(irq && vector==9'o200,"next queued vector");
        put(4,11);check(want_sd && dma_unibus && working && !abort_dma,"shared transfer controls");
        enabled=0;@(negedge clk);check(!irq && !ready && !want_sd,"legacy profile disables bridge");
        $display("PASS IOP bridge: %0d checks",checks);$finish;
    end
endmodule
