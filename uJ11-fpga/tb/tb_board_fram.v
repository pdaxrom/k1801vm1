`timescale 1ns/1ps
module tb_board_fram;
    parameter integer CLK_DIV=1;
    reg clk=0,rst=1,req=0,writing=0,byte_access=0,bank=0;
    reg [15:0] address=0,data=0;
    wire [15:0] value;wire ready,error,busy,cs,sck,mosi,miso;
    reg [7:0] expected[0:131071];
    integer i,j,a,guard,before_count,operations=0;
    reg [31:0] random_state=32'h937fc832;
    always #5 clk=~clk;
    uj11_board_fram #(.CLK_DIV(CLK_DIV)) dut(clk,rst,req,writing,byte_access,bank,address,data,value,ready,error,busy,cs,sck,mosi,miso);
    spi_fram_model memory(cs,sck,mosi,miso);
    task beat(input bit wr,byt,bnk,input [15:0] addr,datum);
        integer pos,limit;
        reg [15:0] answer;
        begin
            @(negedge clk);req=1;writing=wr;byte_access=byt;bank=bnk;address=addr;data=datum;
            pos={bnk,addr};limit=0;before_count=memory.transaction_count;
            while(!ready)begin @(negedge clk);limit=limit+1;if(limit>1000)$fatal(1,"FRAM timeout");end
            operations=operations+1;
            if(!byt && addr[0])begin
                if(!error || memory.transaction_count!=before_count)$fatal(1,"odd word issued SPI");
            end else begin
                if(error)$fatal(1,"unexpected error");
                if(wr)begin expected[pos]=datum[7:0];if(!byt)expected[(pos+1)&131071]=datum[15:8];end
                else begin
                    answer={byt?8'b0:expected[(pos+1)&131071],expected[pos]};
                    if(value!==answer)$fatal(1,"FRAM read bank%0d addr%h got%h expected%h",bnk,addr,value,answer);
                end
            end
            before_count=memory.transaction_count;
            repeat(12)begin @(negedge clk);if(ready || memory.transaction_count!=before_count)$fatal(1,"held request repeated");end
            req=0;repeat(3)@(negedge clk);
        end
    endtask
    initial begin
        for(i=0;i<131072;i=i+1)expected[i]=0;
        repeat(5)@(negedge clk);rst=0;
        for(i=0;i<1024;i=i+1)begin
            random_state=random_state^(random_state<<13);random_state=random_state^(random_state>>17);random_state=random_state^(random_state<<5);
            beat(1,i[0],i[1],random_state[15:0],random_state[31:16]);
            beat(0,i[0],i[1],random_state[15:0],0);
        end
        for(i=0;i<131072;i=i+1)if(memory.memory[i]!==expected[i])$fatal(1,"unexpected FRAM mutation %h",i);
        $display("PASS board FRAM: %0d transactions, both banks, byte/word/odd, held ACK, complete 128 KiB comparison; divider%0d",operations,CLK_DIV);
        $finish;
    end
endmodule
