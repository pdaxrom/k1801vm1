`timescale 1ns/1ps
// Integration of the stateless MMU datapath with the actual board SPI engine.
// CPU/PAR-PDR programming/MMR/IRQ/DMA are NOT present in this harness.
module tb_mmu_fram;
    parameter integer FULL=1;
    reg clk=0,reset=1,request=0,writing=0,byte_access=0,enabled=1,map22=0;
    reg [15:0] virtual_address=0,par=0,pdr=16'o77406,wdata=0;
    wire [21:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm,ready,error,busy,cs,sck,mosi,miso;
    wire [15:0] rdata;
    wire fram_request=request && ram_selected && !(|abort_flags);
    reg [7:0] expected[0:131071];
    integer checks=0,clocks=0,pos,i,m;
    reg [15:0] datum;
    always #5 clk=~clk;
    always @(posedge clk)if(!reset)clocks=clocks+1;
    uj11_mmu_translate mmu(.enabled(enabled),.map22(map22),.writing(writing),
        .invalid_mode(1'b0),.virtual_address(virtual_address),.par(par),.pdr(pdr),
        .physical_address(physical_address),.abort_flags(abort_flags),
        .ram_selected(ram_selected),.io_selected(io_selected),.nxm(nxm));
    uj11_board_fram transport(.clk(clk),.rst(reset),.req(fram_request),
        .write(writing),.byte_access(byte_access),.bank(physical_address[16]),
        .address(physical_address[15:0]),.wdata(wdata),.rdata(rdata),
        .ready(ready),.error(error),.busy(busy),.spi_cs_n(cs),.spi_sck(sck),
        .spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model memory(cs,sck,mosi,miso);

    task beat(input bit wr,byt,input [15:0] va,base,descriptor,data,
              input integer address_expected,input [2:0] flags_expected);
        integer before_cs,guard;
        reg [15:0] answer;
        begin
            @(negedge clk);request=1;writing=wr;byte_access=byt;
            virtual_address=va;par=base;pdr=descriptor;wdata=data;
            before_cs=memory.transaction_count;#1;
            if(physical_address!==address_expected[21:0] || abort_flags!==flags_expected)
                $fatal(1,"FRAM MMU address/rights expected%o got%o flags%b/%b",address_expected,physical_address,flags_expected,abort_flags);
            if(flags_expected!=0 || address_expected>=131072)begin
                repeat(12)begin @(negedge clk);
                    if(fram_request || ready || !cs || memory.transaction_count!=before_cs)
                        $fatal(1,"aborted/NXM/I-O request reached FRAM");
                end
            end else begin
                guard=0;
                while(!ready)begin
                    @(negedge clk);guard=guard+1;
                    if(guard>500)$fatal(1,"FRAM timeout pa%o",physical_address);
                end
                if(!byt && address_expected[0])begin
                    if(!error || memory.transaction_count!=before_cs)$fatal(1,"odd word issued SPI");
                end else begin
                    if(error)$fatal(1,"unexpected FRAM error");
                    if(wr)begin
                        expected[address_expected]=data[7:0];
                        if(!byt)expected[address_expected+1]=data[15:8];
                    end else begin
                        answer={byt?8'b0:expected[address_expected+1],expected[address_expected]};
                        if(rdata!==answer)$fatal(1,"FRAM alias/data pa%o got%h expected%h",physical_address,rdata,answer);
                    end
                end
                before_cs=memory.transaction_count;
                repeat(12)begin @(negedge clk);
                    if(ready || memory.transaction_count!=before_cs)$fatal(1,"held request repeated");
                end
            end
            checks=checks+1;
            request=0;repeat(3)@(negedge clk);
        end
    endtask
    initial begin
        for(i=0;i<131072;i=i+1)expected[i]=0;
        repeat(5)@(negedge clk);reset=0;
        // Every physical word. VA is deliberately in the virtual I/O window:
        // enabled mapping must select physical RAM, not decode VA as a CSR.
        for(pos=0;pos<131072;pos=pos+(FULL!=0?2:1022))begin
            map22=pos[13];datum=pos[16:1]^16'hc137;
            beat(1,0,{3'b111,pos[12:0]},{5'b0,pos[16:13],7'b0},16'o77406,datum,pos,0);
        end
        for(m=0;m<2;m=m+1)begin
            map22=m[0];
            for(pos=0;pos<131072;pos=pos+(FULL!=0?2:1022))
                beat(0,0,{3'b010,pos[12:0]},{5'b0,pos[16:13],7'b0},16'o77406,0,pos,0);
        end
        // Byte access at the 64 KiB boundary and the last installed byte.
        for(m=0;m<2;m=m+1)begin
            map22=m[0];
            beat(1,1,16'h1fff,16'h0380,16'o77406,16'h0039,65535,0);
            beat(1,1,0,16'h0400,16'o77406,16'h00c6,65536,0);
            beat(1,1,16'h1fff,16'h0780,16'o77406,16'h00ad,131071,0);
            beat(0,0,16'h1ffe,16'h0380,16'o77406,0,65534,0);
            beat(0,0,0,16'h0400,16'o77406,0,65536,0);
            beat(0,1,16'h1fff,16'h0780,16'o77406,0,131071,0);
            beat(0,0,16'h1fff,16'h0780,16'o77406,0,131071,0); // odd
            beat(1,0,0,16'h0800,16'o77406,16'habcd,131072,0); // first NXM
            beat(1,0,0,16'h0000,16'o77402,16'habcd,0,1); // read-only
            beat(1,0,16'h0040,0,16'o000006,16'habcd,64,2); // length
            beat(1,0,0,0,16'o77400,16'habcd,0,4); // nonresident
        end
        map22=0;beat(1,0,0,16'h0f80,16'o77406,16'hffff,4186112,0); // 18-bit I/O
        map22=1;beat(1,0,0,16'h0f80,16'o77406,16'hffff,253952,0); // 22-bit NXM
        beat(1,0,0,16'hff80,16'o77406,16'hffff,4186112,0); // 22-bit I/O
        enabled=0;beat(1,0,16'he000,0,0,16'hffff,4186112,0); // disabled I/O
        for(i=0;i<131072;i=i+1)if(memory.memory[i]!==expected[i])$fatal(1,"unexpected FRAM mutation %o",i);
        $display("PASS MMU + SPI FRAM: %0d beats FULL=%0d, %0d clocks; bank boundary, complete 128 KiB comparison, abort/NXM/I-O inhibit, odd/byte/held request",checks,FULL,clocks);
        $finish;
    end
endmodule
