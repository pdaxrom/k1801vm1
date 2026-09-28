`timescale 1ns/1ps
// A real CPU TSTSET competes with DMA through the production SRAM arbiter.
// Check the read/write gap and release after either completion or bus abort.
module tb_locked;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1;always #5 clk=~clk;
    wire cr,cw,cb,lock,cready,waiting;
    wire [21:0] ca;wire [15:0] cd,pc,psw;
    reg dr=0;wire dready,mr,mw;wire [19:0] ma;wire [1:0] ml;wire [15:0] md;
    reg ack=0,seen=0,started=0,completed=0,wrote=0;
    reg [15:0] rd=0,ram[0:32767];integer delay_count=0,scenario,checks=0,i,cycles=0;
    wire failed=scenario==1 && cw && ca=='o1000;
    uj11_mmu_cpu cpu(.clk(clk),.reset(reset),.halt_button(1'b0),.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(16'b0),
        .mem_request(cr),.mem_write(cw),.mem_byte(cb),.mem_lock(lock),.mem_address(ca),.mem_write_data(cd),
        .mem_ready(cready),.mem_error(failed),.mem_read_data(rd),.waiting(waiting),.pc(pc),.psw(psw),.debug_register_address(5'b0));
    uj11_mmu_sram_arbiter arbiter(.clk(clk),.reset(reset),.cpu_request(cr),.cpu_write(cw),.cpu_lock(lock),
        .cpu_address(ca[20:1]),.cpu_lanes(2'b11),.cpu_data(cd),.cpu_ready(cready),
        .dma_request(dr),.dma_write(1'b1),.dma_address(22'o1000),.dma_data(16'hbeef),.dma_ready(dready),
        .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(ack));
    always @(posedge clk)begin
        ack<=0;
        if(reset)begin seen<=0;delay_count<=0;cycles<=0;dr<=0;started<=0;completed<=0;wrote<=0;end
        else begin
            cycles<=cycles+1;
            if(cycles>10000)$fatal(1,"locked transfer timeout scenario %0d",scenario);
            if(lock && cr && ca=='o1000 && !started)begin dr<=1;started<=1;end
            if(dready)begin
                if(lock)$fatal(1,"DMA entered a locked read/modify/write");
                if(scenario==0 && !wrote)$fatal(1,"DMA passed TSTSET store");
                dr<=0;completed<=1;
            end
            if(!mr)begin seen<=0;delay_count<=0;end
            else if(!seen)begin
                if(delay_count==2)begin
                    seen<=1;ack<=1;rd<=ram[ma];
                    if(mw && !(failed && arbiter.state==1))ram[ma]<=md;
                    if(mw && arbiter.state==1 && ca=='o1000)begin
                        if(!lock)$fatal(1,"TSTSET store is not locked");
                        if(cd!='o1235)$fatal(1,"TSTSET read observed competing DMA");
                        wrote<=1;
                    end
                end else delay_count<=delay_count+1;
            end
        end
    end
    task check(input bit yes,input string why);checks++;if(!yes)$fatal(1,"%s",why);endtask
    initial begin
        for(i=0;i<32768;i++)ram[i]=0;
        ram['o4000/2]='o12706;ram['o4002/2]='o2000;
        ram['o4004/2]='o12701;ram['o4006/2]='o1000;
        ram['o4010/2]='o7211;ram['o4012/2]=1;
        ram[2]='o7000;ram[3]='o340;ram['o7000/2]=1;
        for(scenario=0;scenario<2;scenario++)begin
            reset=1;repeat(4)@(negedge clk);ram['o1000/2]='o1234;reset=0;
            wait(waiting && completed);@(negedge clk);
            check(!lock,"lock released after completion/abort");
            check(ram['o1000/2]==16'hbeef,"pending DMA completes after lock");
            check(cpu.debug_register_data=='o1234,"R0 loaded before store/abort");
            check(pc==(scenario==0?'o4014:'o7002),"normal completion or bus trap");
            if(scenario==1)check(ram['o1776/2]=='o340,"failed store preserves flags in trap frame");
        end
        $display("PASS MMU locked bus: %0d checks",checks);$finish;
    end
endmodule
