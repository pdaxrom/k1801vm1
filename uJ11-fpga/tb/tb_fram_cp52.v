`timescale 1ns/1ps
module tb_fram_cp52 #(parameter integer CLK_DIV=1);
    reg clk=0,rst=1,req=0,writing=0,byte_access=0,bank=0,keep_read=1,close_read=0;
    reg [15:0] address=0,data=0;
    wire [15:0] value;
    wire ready,error,busy,cs,sck,mosi,miso;
    integer sck_count=0,checks=0,i,phase;
    time last_cs_high=0,last_cs_low=0,last_sck_high=0,last_sck_low=0;
    // 29.41 MHz is just below the board's nominal 29.56 MHz. The protocol
    // monitor checks MR45V100A tSLCH/tSHSL/tCHSH (10 ns) and tCH/tCL (13 ns).
    always #17 clk=~clk;
    uj11_board_fram #(.CLK_DIV(CLK_DIV)) dut(
        .clk(clk),.rst(rst),.req(req),.write(writing),.byte_access(byte_access),.bank(bank),
        .keep_read(keep_read),.close_read(close_read),.address(address),.wdata(data),
        .rdata(value),.ready(ready),.error(error),.busy(busy),
        .spi_cs_n(cs),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model memory(cs,sck,mosi,miso);
    always @(negedge cs)begin
        if(!rst && $time-last_cs_high<10)$fatal(1,"tSHSL");
        last_cs_low=$time;
    end
    always @(posedge cs)begin
        if(!rst && $time-last_sck_high<10)$fatal(1,"tCHSH");
        last_cs_high=$time;
    end
    always @(posedge sck)begin
        if(!rst && (cs || $time-last_cs_low<10 || $time-last_sck_low<13))$fatal(1,"SCK setup/low time");
        last_sck_high=$time;
        if(!cs)sck_count=sck_count+1;
    end
    always @(negedge sck)begin
        if(!rst && $time-last_sck_high<13)$fatal(1,"SCK high time");
        last_sck_low=$time;
    end
    task beat(input bit wr,byt,bnk,input [15:0] addr,datum,expected,
              input integer edges,transactions,input bit parked);
        integer start_edges,start_cs,guard;
        begin
            @(negedge clk);req=1;writing=wr;byte_access=byt;bank=bnk;address=addr;data=datum;
            start_edges=sck_count;start_cs=memory.transaction_count;guard=0;
            while(!ready)begin
                @(negedge clk);guard=guard+1;if(guard>1000)$fatal(1,"timeout addr%h",addr);
            end
            if(error!==(!byt && addr[0]))$fatal(1,"odd error");
            if(!wr && !error && value!==expected)$fatal(1,"read %h got%h expected%h",addr,value,expected);
            if(sck_count-start_edges!=edges || memory.transaction_count-start_cs!=transactions)
                $fatal(1,"wire count addr%h: %0d SCK/%0d CS expected%0d/%0d",addr,
                    sck_count-start_edges,memory.transaction_count-start_cs,edges,transactions);
            if(cs!==!parked || sck || busy)$fatal(1,"completion pin/state contract");
            // Held requests and arbitrarily long pauses may not clock another
            // bit or repeat ready. rdata remains stable until the next request.
            repeat(19)begin
                @(negedge clk);
                if(ready || busy || sck || sck_count!=start_edges+edges)$fatal(1,"held request repeated");
                if(!wr && !error && !addr[0] && value!==expected)$fatal(1,"idle data changed");
            end
            req=0;repeat(3)@(negedge clk);checks=checks+1;
        end
    endtask
    task close;
        integer before_edges;
        begin
            before_edges=sck_count;close_read=1;repeat(3)@(negedge clk);
            if(!cs || busy || ready || sck_count!=before_edges)$fatal(1,"idle close");
            close_read=0;
        end
    endtask
    initial begin
        for(i=0;i<131072;i=i+1)memory.memory[i]=8'(i^(i>>8)^(i>>16));
        repeat(5)@(negedge clk);rst=0;
        beat(0,0,0,16'h1000,0,16'h1110,48,1,1);
        // More than one low-address-byte wrap without repeating a header.
        for(i=1;i<=300;i=i+1)begin
            beat(0,0,0,16'(4096+2*i),0,
                {memory.memory[4097+2*i],memory.memory[4096+2*i]},16,0,1);
        end
        // Branch backwards, same address, explicit overlay/device close.
        beat(0,0,0,16'h1000,0,16'h1110,48,1,1);
        beat(0,0,0,16'h1000,0,16'h1110,48,1,1);
        close;
        beat(0,0,0,16'h1002,0,16'h1312,48,1,1);
        // A same-next-address write must issue WREN + fresh WRITE, then a
        // subsequent read must observe it, never a prefetched/stale value.
        beat(1,0,0,16'h1004,16'ha55a,0,56,2,0);
        beat(0,0,0,16'h1004,0,16'ha55a,48,1,1);
        beat(1,1,0,16'h1006,16'h00cc,0,48,2,0);
        beat(0,1,0,16'h1006,0,16'h00cc,40,1,0);
        beat(0,1,0,16'h1007,0,16'h0017,40,1,0);
        beat(0,0,0,16'h1008,0,16'h1918,48,1,1);
        beat(0,0,0,16'h1009,0,0,0,0,0);
        // Permission is demand qualification, not merely a hint on a miss.
        beat(0,0,0,16'h2000,0,16'h2120,48,1,1);
        keep_read=0;
        beat(0,0,0,16'h2002,0,16'h2322,48,1,0);
        keep_read=1;
        beat(0,0,0,16'h2004,0,16'h2524,48,1,1);
        // Native address wrap cannot continue into physical bank one. Bank
        // one remains supported by ordinary requests, with no retention.
        beat(0,0,0,16'hfffc,0,16'h0203,48,1,1);
        beat(0,0,0,16'hfffe,0,16'h0001,48,1,0);
        beat(0,0,0,16'h0000,0,16'h0100,48,1,1);
        beat(0,0,1,16'h0002,0,16'h0203,48,1,0);
        beat(0,0,1,16'h0004,0,16'h0405,48,1,0);
        beat(0,0,0,16'h0006,0,16'h0706,48,1,1);
        // Abort a parked read and every header/data byte phase by reset.
        for(phase=0;phase<=6;phase=phase+1)begin
            if(phase!=0)begin
                close;
                @(negedge clk);req=1;address=16'h3000;
                repeat(phase*8-1)@(posedge sck);
                @(negedge clk);
            end
            rst=1;req=0;repeat(4)@(negedge clk);
            if(!cs || sck || busy || ready)$fatal(1,"reset state");
            rst=0;
            beat(0,0,0,16'h3000,0,16'h3130,48,1,1);
        end
        close;
        $display("PASS CP52 FRAM: %0d checked beats; sequential/redirect/byte/odd/write/bank/reset/hold/CS timing, divider%0d",checks,CLK_DIV);
        $finish;
    end
    initial begin #20000000;$fatal(1,"CP52 protocol watchdog");end
endmodule
