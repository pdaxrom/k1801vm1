`timescale 1ns/1ps
module tb_storage_ram #(parameter WORDS=4864);
`ifdef UJ11_IOP_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,enable=0;always #20.833 clk=~clk;
    reg [12:0] address=0;reg [3:0] we=0;reg [31:0] wd=0;
    wire [31:0] rd;wire ready;reg [31:0] golden[0:WORDS-1];
    reg [7:0] sector_address=0;reg sector_request=0,sector_write=0;reg [15:0] sector_data=0;
    wire [15:0] sector_read;
    integer checks=0;
    localparam BASE=4608,SECTOR=WORDS-128;
    uj11_mmu_iop_ram ram(.clk(clk),.reset(reset),.enable(enable),.address(address),
        .write_enable(we),.write_data(wd),.data(rd),.storage_enabled(),.ready(ready),
        .sector_request(sector_request),.sector_write(sector_write),.sector_address(sector_address),
        .sector_write_data(sector_data),.sector_read_data(sector_read));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s at address %h: read %h expected %h",why,address,rd,golden[address]);checks++;end
    endtask
    task completion;
        integer ticks;
        begin
            ticks=0;
            do begin @(negedge clk);ticks++;end while(!ready && ticks<20);
            check(ready,"serialized completion timeout");
        end
    endtask
    task access(input integer a,input [3:0] lanes,input [31:0] value);
        begin
            enable=0;@(negedge clk);address=13'(a);we=lanes;wd=value;enable=1;
            #1;
            if(a<BASE)begin check(ready,"full-width bank takes one clock");@(negedge clk);end
            else begin check(!ready,"tail cannot acknowledge before both halves");completion();end
            if(!lanes)check(rd===golden[a],"RAM read");
            enable=0;@(negedge clk);
        end
    endtask
    task read_sector(input integer a);
        begin
            sector_address=8'(a);sector_request=1;sector_write=0;@(negedge clk);
            sector_request=0;@(negedge clk);
            check(sector_read===golden[SECTOR+a/2][16*(a%2)+:16],"sector read and half-word selection");
        end
    endtask
    initial begin
        $readmemh("build/hc7000-mmu-iop/firmware.mem",golden);
        repeat(5)@(negedge clk);reset=0;
        for(integer i=0;i<WORDS;i++)access(i,0,0);
        for(integer i=0;i<WORDS;i++)begin
            golden[i]=32'hdeadc0de^(i*32'h01030507);
            access(i,15,golden[i]);access(i,0,0);
            for(integer lane=0;lane<4;lane++)begin
                golden[i][8*lane+:8]=8'((32'hab1289ef^(i*32'h07050301))>>(8*lane));
                access(i,4'(1<<lane),32'hab1289ef^(i*32'h07050301));access(i,0,0);
            end
        end
        // All nine full-width banks support adjacent-cycle reads, including
        // addresses above 16 KiB and every word of the reserved stack.
        enable=1;we=0;
        for(integer i=0;i<BASE;i++)begin
            address=(i%9)*512+i/9;@(negedge clk);
            check(rd===golden[address],"full-width bank isolation and adjacent reads");
        end
        enable=0;we=15;wd=0;repeat(3)@(negedge clk);we=0;
        access(address,0,0);
        for(integer i=0;i<256;i++)begin
            address=BASE+i%128;we=15;wd=32'hfedc0000+i;enable=1;
            sector_address=8'(i);sector_data=16'hcafe^16'(i*257);sector_write=1;sector_request=1;
            #1;check(!ready,"sector has priority over cold-data write");@(negedge clk);
            golden[SECTOR+i/2][16*(i%2)+:16]=sector_data;
            sector_request=0;sector_write=0;completion();golden[address]=wd;enable=0;we=0;
            access(BASE+i%128,0,0);access(SECTOR+i/2,0,0);
            address=i;enable=1;sector_address=8'(i);sector_request=1;
            #1;check(ready,"full-width bank unaffected by sector read");@(negedge clk);
            check(rd===golden[i],"concurrent private-bank read");
            sector_request=0;enable=0;@(negedge clk);
            check(sector_read===sector_data,"sector result capture");
            access(BASE+i%128,0,0);
            check(sector_read===sector_data,"sector result held across two CPU RAM beats");
        end
        // Preempt before the low read, between low/high, or after high was
        // issued. Pending results must be captured before the engine reuses
        // the physical read port, including sustained sector traffic.
        for(integer position=0;position<3;position++)for(integer i=0;i<128;i++)begin
            enable=0;@(negedge clk);address=BASE+i;we=0;enable=1;
            repeat(position)@(negedge clk);
            sector_request=1;sector_write=0;
            for(integer beat=0;beat<4;beat++)begin
                sector_address=8'(i+beat);@(negedge clk);
                if(position<2)check(!ready,"partly read word stalls behind sector burst");
            end
            sector_request=0;
            if(!ready)completion();
            check(rd===golden[BASE+i],"tail read survives port preemption");
            enable=0;@(negedge clk);
        end
        address=BASE+72;we=15;wd=32'h12345678;enable=1;sector_request=1;sector_write=1;
        for(integer i=0;i<256;i++)begin
            sector_address=8'(i);sector_data=0;#1;check(!ready,"padding burst arbitration");
            @(negedge clk);golden[SECTOR+i/2][16*(i%2)+:16]=0;
        end
        sector_request=0;sector_write=0;completion();golden[BASE+72]=wd;enable=0;we=0;
        access(BASE+72,0,0);for(integer i=0;i<256;i++)read_sector(i);
        // Reset prevents writes/capture while asserted and cancels the
        // pending tail read without resetting stored data.
        enable=0;@(negedge clk);address=BASE+72;enable=1;we=0;@(negedge clk);
        sector_address=255;sector_request=1;reset=1;sector_write=1;
        sector_data=16'habcd;we=15;wd=32'hdeadbeef;
        repeat(2)@(negedge clk);reset=0;enable=0;we=0;sector_request=0;sector_write=0;
        access(BASE+72,0,0);access(WORDS-1,0,0);read_sector(255);
        for(integer i=0;i<WORDS;i++)access(i,0,0);
        $display("PASS MMU shared storage RAM: %0d checks",checks);$finish;
    end
endmodule
