`timescale 1ns/1ps
module tb_sd_host;
    reg clk=0,reset=1,request=0,writing=0,init_valid=0,block_mode=1,bw=0;
    always #20.833333 clk=!clk;
    reg [31:0] lba=0;
    reg [8:0] ba=0;
    reg [7:0] bd=0;
    wire ready,initialized,done,error,cs,start,busy,byte_done,sck,mosi,miso;
    wire [7:0] br,tx,divider,rx;
    integer reads,index,ignored,capacity=1,write_failure=0,corrupt_read=0;
    bk_sd_host dut(clk,reset,request,writing,lba,ready,initialized,done,error,ba,bw,bd,br,
                   cs,init_valid,block_mode,start,tx,divider,busy,byte_done,rx);
    spi_byte_master shifter(clk,reset,start,tx,divider,miso,sck,mosi,busy,byte_done,rx);
    sd_spi_model #(.IMAGE("build/sd-host-test.img")) card(cs,sck,mosi,miso,reads);
    function [7:0] original(input integer s,input integer b);original=s*31+b*13;endfunction
    task transfer(input wr,input [31:0] address,input expect_error);
        begin
            wait(ready);@(negedge clk);writing=wr;lba=address;request=1;
            @(negedge clk);request=0;
            wait(done);#1;
            if(error!==expect_error||!cs||busy)$fatal(1,"SD host completion error=%0d expected=%0d",error,expect_error);
            @(negedge clk);
        end
    endtask
    task check(input integer sector,input modified);
        begin
            for(index=0;index<512;index=index+1)begin
                @(negedge clk);ba=index;repeat(2)@(negedge clk);
                if(br!==(modified ? (index^8'h5a)&255 : original(sector,index)))
                    $fatal(1,"SD buffer byte %0d got=%02h",index,br);
            end
        end
    endtask
    initial begin
        ignored=$value$plusargs("sdhc=%d",capacity);block_mode=capacity!=0;
        ignored=$value$plusargs("write-fail-at=%d",write_failure);
        ignored=$value$plusargs("corrupt-read-at=%d",corrupt_read);
        #200;reset=0;repeat(5)@(negedge clk);
        if(ready||initialized||!cs)$fatal(1,"Runtime host started without BIOS adoption");
        init_valid=1;wait(ready);
        transfer(0,31,corrupt_read==1);check(31,0);
        for(index=0;index<512;index=index+1)begin
            @(negedge clk);ba=index;bd=index^8'h5a;bw=1;
        end
        @(negedge clk);bw=0;
        transfer(1,17,write_failure!=0);
        transfer(0,17,0);check(17,write_failure==0);
        transfer(0,32,1);
        if(!block_mode)transfer(0,32'h00800000,1);
        transfer(0,31,0);check(31,0);
        if(reads!=3||card.writes!=1)$fatal(1,"SD host repeated a held request");
        $display("PASS runtime SD host: SDHC=%0d write_error=%0d, 512-byte CRC, read/write, busy, bounds, recovery",capacity,write_failure);
        $finish;
    end
    initial begin #20000000;$fatal(1,"Runtime SD host timeout state=%0d",dut.state);end
endmodule
