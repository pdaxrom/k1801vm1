`timescale 1ns/1ps
module tb_mmu;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0; always #5 clk=~clk;
    reg reset=1,peripheral_reset=0,request=0,writing=0,byte_access=0;
    reg [15:0] virtual_address=0,write_data=0;
    reg [1:0] mode=0;
    reg data_space=0,physical=0,console=0;
    reg [21:0] physical_address=0;
    reg instruction_start=0,delta_valid=0;
    reg [15:0] instruction_pc=0;
    reg [2:0] delta_register=0;
    reg [4:0] delta_amount=0;
    wire ready;wire [2:0] fault;wire [15:0] read_data;
    wire bus_request,bus_write,bus_byte;wire [21:0] bus_address;wire [15:0] bus_data;
    reg bus_ready=0,bus_error=0;reg [15:0] bus_read_data=0;
    wire [15:0] mmr0,mmr1,mmr2,mmr3;
    uj11_mmu dut(.*);
    reg [15:0] ram[0:1048575];
    integer latency=0,beats=0,checks=0,i,m,s,p,base,before_beats;
    reg bus_seen=0;
    reg [21:0] last_pa;
    reg [15:0] result,held0,held1,held2;
    always @(posedge clk) begin
        bus_ready<=0;
        if(reset || peripheral_reset || !bus_request) begin latency<=0;bus_seen<=0;end
        else if(!bus_seen) begin
            if(latency==3) begin
                bus_seen<=1;bus_ready<=1;beats<=beats+1;last_pa<=bus_address;
                bus_error<=bus_address>=22'h200000 && bus_address<22'h3fe000;
                bus_read_data<=bus_address<22'h200000 ? ram[bus_address[20:1]] : 16'hcafe;
                if(bus_write && bus_address<22'h200000) begin
                    if(!bus_byte || !bus_address[0])ram[bus_address[20:1]][7:0]<=bus_data[7:0];
                    if(!bus_byte || bus_address[0])ram[bus_address[20:1]][15:8]<=bus_data[15:8];
                end
            end else latency<=latency+1;
        end
    end
    task check(input bit condition,input [767:0] description);
        begin checks=checks+1;if(!condition)$fatal(1,"%0s (check %0d)",description,checks);end
    endtask
    task access(input [21:0] a,input bit wr,input bit byt,input [15:0] d,
                input [1:0] md,input bit ds,input bit phys,input bit dbg,input [2:0] expected);
        integer timeout,n,count_at_ready;
        begin
            @(negedge clk);request=1;virtual_address=a[15:0];physical_address=a;
            writing=wr;byte_access=byt;write_data=d;mode=md;data_space=ds;physical=phys;console=dbg;
            timeout=0;
            while(!ready && timeout<40)begin @(negedge clk);timeout=timeout+1;end
            check(ready,"access timeout");
            if(fault!=expected)$fatal(1,"a=%o wr=%d mode=%d ds=%d fault=%d expected=%d",a,wr,md,ds,fault,expected);
            checks=checks+1;result=read_data;count_at_ready=beats;
            repeat(3)begin @(negedge clk);check(ready && fault==expected,"held completion");end
            check(beats==count_at_ready,"held request repeats bus transaction");
            request=0;@(negedge clk);@(negedge clk);
        end
    endtask
    task csr(input [21:0] a,input [15:0] d);
        access(a,1,0,d,0,0,1,1,0);
    endtask
    function [21:0] apr_address(input integer md,input integer ds,input integer page,input bit par);
        apr_address=(md==0 ? 22'o17772300 : md==1 ? 22'o17772200 : 22'o17777600)+ds*16+page*2+(par ? 32 : 0);
    endfunction
    task map(input integer md,input integer ds,input integer page,input [15:0] par,input [15:0] pdr);
        begin csr(apr_address(md,ds,page,1),par);csr(apr_address(md,ds,page,0),pdr);end
    endtask
    initial begin
        for(i=0;i<1048576;i=i+1)ram[i]=i[15:0]^16'h5a5a;
        repeat(3)@(negedge clk);reset=0;
        access(22'o177560,0,0,0,0,0,0,0,0);
        check(last_pa==22'o17777560 && result==16'hcafe,"unmapped I/O canonicalization");
        access(22'o1234,1,0,16'habcd,0,0,0,0,0);
        check(ram['o516]==16'habcd,"unmapped SRAM write");
        before_beats=beats;access(22'o1235,1,0,0,0,0,0,0,1);
        check(beats==before_beats,"odd word has no bus side effect");
        // Every architecturally addressable PAR/PDR, including byte writes.
        for(m=0;m<4;m=m+1)if(m!=2)for(s=0;s<2;s=s+1)for(p=0;p<8;p=p+1)begin
            map(m,s,p,16'h1000+(m*16+s*8+p)*16,16'o177406);
            access(apr_address(m,s,p,1),1,1,16'h0043,0,0,1,1,0);
            access(apr_address(m,s,p,1)+1,1,1,16'h1200,0,0,1,1,0);
            access(apr_address(m,s,p,1),0,0,0,0,0,1,1,0);
            check(result==16'h1243,"PAR byte merge");
            map(m,s,p,16'h1000+(m*16+s*8+p)*16,16'o177406);
        end
        csr(22'o17772516,16'o27);csr(22'o17777572,1);
        for(m=0;m<4;m=m+1)if(m!=2)for(s=0;s<2;s=s+1)for(p=0;p<8;p=p+1)begin
            base=(16'h1000+(m*16+s*8+p)*16)*64;
            access(p*8192+42,0,0,0,m,s,0,0,0);
            check(last_pa==base+42,"mode/I-D/page PAR lookup");
            access(p*8192+42,1,0,16'h9876,m,s,0,0,0);
            check(ram[(base+42)/2]==16'h9876,"mapped write commits");
            access(apr_address(m,s,p,0),0,0,0,0,0,1,1,0);
            check(result==16'o177506,"PDR.W recorded");
        end
        // Disabling split selects I tables even on data accesses.
        csr(22'o17772516,16'o20);
        access(42,0,0,0,3,1,0,0,0);check(last_pa==(16'h1300*64+42),"unified user mapping");
        // Relocation disabled/18/22-bit canonical I/O and wrap.
        map(0,0,0,16'o177600,16'o177406);csr(22'o17772516,0);
        access(22'o40,0,0,0,0,0,0,0,0);check(last_pa==22'o17760040,"18-bit I/O remap");
        csr(22'o17772516,16'o20);
        access(22'o40,0,0,0,0,0,0,0,0);check(last_pa==22'o17760040,"22-bit high I/O");
        map(0,0,0,16'o7600,16'o177406);
        access(22'o40,0,0,0,0,0,0,0,0);check(last_pa==22'o760040,"22-bit low RAM is not 18-bit I/O alias");
        map(0,0,0,16'hffff,16'o177406);
        access(128,0,0,0,0,0,0,0,0);check(last_pa==64,"22-bit PAR sum wraps");
        map(0,0,0,16'h8000,16'o177406);
        access(0,0,0,0,0,0,0,0,2);check(last_pa==22'h200000,"NXM does not truncate to installed RAM");
        // First abort freezes MMR0/1/2; console faults do not replace it.
        @(negedge clk);instruction_pc=16'o1234;instruction_start=1;
        @(negedge clk);instruction_start=0;delta_valid=1;delta_register=2;delta_amount=2;
        @(negedge clk);delta_register=5;delta_amount=30;
        @(negedge clk);delta_valid=0;
        check(mmr1==16'hf512 && mmr2==16'o1234,"MMR1 signed deltas and MMR2 fetch PC");
        map(0,0,0,0,16'o177402);before_beats=beats;
        access(0,1,0,16'hffff,0,0,0,0,3);
        check(beats==before_beats && mmr0[15:13]==1,"write protection blocks bus");
        held0=mmr0;held1=mmr1;held2=mmr2;
        access(apr_address(0,0,0,0),0,0,0,0,0,1,1,0);
        check(result==16'o177502,"faulting write sets PDR.W");
        @(negedge clk);instruction_start=1;instruction_pc=16'o5555;delta_valid=1;
        @(negedge clk);instruction_start=0;delta_valid=0;
        access(0,0,0,0,2,0,0,1,3);
        check(mmr0==held0 && mmr1==held1 && mmr2==held2,"first-fault freeze and console isolation");
        csr(22'o17777572,1);map(0,0,0,0,16'o6);before_beats=beats;
        access(64,0,0,0,0,0,0,0,3);check(mmr0[15:13]==2 && beats==before_beats,"upward length fault");
        csr(22'o17777572,1);map(0,0,0,0,16'o1016);
        access(64,0,0,0,0,0,0,0,3);check(mmr0[15:13]==2,"downward length fault");
        csr(22'o17777572,1);map(0,0,0,0,16'o177400);
        access(0,0,0,0,0,0,0,0,3);check(mmr0[15:13]==4,"nonresident read");
        csr(22'o17777572,1);map(0,0,0,0,16'o177406);
        access(0,0,0,0,2,0,0,0,3);check(mmr0[15] && mmr0[6:5]==2,"reserved mode abort metadata");
        // Debug translation errors while unfrozen never set guest MMR flags.
        csr(22'o17777572,1);held0=mmr0;held1=mmr1;held2=mmr2;
        access(0,0,0,0,2,0,0,1,3);
        check(mmr0==held0 && mmr1==held1 && mmr2==held2,"console fault while guest not frozen");
        // Physical upper byte keeps the other lane and bypasses MMU protection.
        ram[22'h1ffffe/2]=16'habcd;
        access(22'h1fffff,1,1,16'h1200,0,0,1,1,0);
        check(ram[22'h1ffffe/2]==16'h12cd,"physical console high byte at last SRAM address");
        @(negedge clk);peripheral_reset=1;
        @(negedge clk);peripheral_reset=0;
        check(mmr0==0 && mmr1==0 && mmr2==0 && mmr3==0,"RESET clears MMU controls");
        access(apr_address(0,0,0,0),0,0,0,0,0,1,1,0);
        check(result==16'o177406,"RESET preserves APR contents");
        $display("PASS MMU transactions: %0d checks, %0d physical beats",checks,beats);$finish;
    end
    initial begin #2000000;$fatal(1,"global timeout");end
endmodule
