`timescale 1ns/1ps
module tb_ubmap;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,enabled=0;always #5 clk=~clk;
    reg request=0,write=0;reg [5:0] address=0;reg [1:0] lanes=3;reg [15:0] wdata=0;
    wire [15:0] rdata;wire ready;
    reg dma_request=0;reg [21:0] dma_address=0;wire dma_ready,dma_error,memory_request;
    wire [21:0] memory_address;reg memory_ready=0;
    integer checks=0;
    uj11_mmu_ubmap dut(.*);
    task check(input bit ok,input string why);begin if(!ok)$fatal(1,"%s",why);checks++;end endtask
    task csr(input bit wr,input [5:0] addr,input [15:0] value,input [1:0] lane,output [15:0] result);
        begin @(negedge clk);request=1;write=wr;address=addr;wdata=value;lanes=lane;
            do @(negedge clk);while(!ready);result=rdata;request=0;repeat(2)@(negedge clk);end
    endtask
    reg [15:0] result;
    task put(input [5:0] a,input [15:0] v);csr(1,a,v,3,result);endtask
    task dma(input [21:0] a,input [21:0] want,input bit fault);
        integer n;
        begin @(negedge clk);dma_address=a;dma_request=1;n=0;
            while(!memory_request && !dma_ready)begin @(negedge clk);n++;if(n>12)$fatal(1,"map timeout");end
            check(memory_address==want,"translated address");check(dma_error==fault,"NXM flag");
            if(fault)check(!memory_request && dma_ready,"NXM completes without SRAM request");
            else begin repeat(5)@(negedge clk);check(memory_address==want && !dma_ready,"held mapped transaction");
                memory_ready=1;@(negedge clk);check(dma_ready,"DMA completion");end
            dma_request=0;memory_ready=0;repeat(3)@(negedge clk);
        end
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        dma(22'h123456,22'h123456,0);enabled=1;
        for(integer i=0;i<32;i++)begin
            put(2*i,16'hfffe);put(2*i+1,i);
            csr(0,2*i,0,3,result);check(result==16'hfffe,"low map read");
            csr(0,2*i+1,0,3,result);check(result==i,"high map read");
        end
        dma(22'h00b002,22'h061000,0); // page 5 + carry from byte offset
        dma(22'h03e000,22'h3fe000,1); // last page hardwired IO
        dma(22'h03dffe,22'h1f1ffc,0); // page 30 at last word
        put(2*4+1,16'hffff);csr(0,9,0,3,result);check(result==63,"six high bits only");
        dma(22'h008002,22'h000000,0); // 4 MiB modulo wrap
        csr(1,8,16'h5500,2,result);csr(0,8,0,3,result);check(result==16'h55fe,"high-byte merge");
        csr(1,8,16'h00ab,1,result);csr(0,8,0,3,result);check(result==16'h55aa,"low-byte/even mask");
        put(9,32);dma(22'h008000,22'h2055aa,1);
        @(negedge clk);reset=1;@(negedge clk);reset=0;
        csr(1,8,16'h0055,1,result);csr(0,8,0,3,result);check(result==16'h0054,"partial first write after reset");
        csr(0,9,0,3,result);check(result==0,"reset hides stale high map word");
        dma(22'h008000,22'h000054,0);
        $display("PASS MMU UBMAP: %0d checks",checks);$finish;
    end
    initial begin #100000;$fatal(1,"timeout");end
endmodule
