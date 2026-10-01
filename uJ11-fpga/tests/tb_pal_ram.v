`timescale 1ns/1ps
module tb_pal_ram;
`ifdef UJ11_VIDEO_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg wc=0,rc=0,we=0,re=0;
    always #10 wc=~wc;
    always #7.8125 rc=~rc;
    reg [9:0] wa=0,ra=0;reg [7:0] wd=0;wire [7:0] rd;
    uj11_pal_ram ram(.write_clk(wc),.write_enable(we),.write_address(wa),.write_data(wd),
        .read_clk(rc),.read_enable(re),.read_address(ra),.read_data(rd));
    function [7:0] expected(input integer a);expected=(a*73)^(a>>8);endfunction
    initial begin
        #200;
        for(integer a=0;a<1024;a++)begin
            @(negedge wc);we=1;wa=a;wd=expected(a);
        end
        @(negedge wc);we=0;
        for(integer a=0;a<1024;a++)begin
            @(negedge rc);re=1;ra=a;@(posedge rc);#1;
            if(rd!==expected(a))$fatal(1,"EBR address %d got %h expected %h",a,rd,expected(a));
        end
        @(negedge rc);re=0;ra=0;
        repeat(5)@(posedge rc);#1;
        if(rd!==expected(1023))$fatal(1,"EBR read-enable hold");
        $display("PASS PAL 1024x9 EBR address/byte/latency/gating");$finish;
    end
endmodule
