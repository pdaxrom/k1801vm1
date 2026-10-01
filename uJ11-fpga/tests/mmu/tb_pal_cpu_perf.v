`timescale 1ns/1ps
// Steady-state CPU throughput across four complete PAL frames. No disk load.
// MODE 0 = original arbiter, 1 = PAL disabled, 2 = 4 bpp, 3 = 8 bpp.
module tb_pal_cpu_perf #(parameter integer MODE=1);
    reg clk=0,vclk=0,reset=1,power_on=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    wire request,writing,byte_access,cpu_lock,ready,error,retire,stopped;
    wire [21:0] address;wire [15:0] wdata,rdata,pc,regdata;
    uj11_mmu_cpu dut(.clk(clk),.reset(reset),.halt_button(1'b0),
        .irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(16'b0),.irq_ack(),.peripheral_reset(),
        .mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_lock(cpu_lock),
        .mem_address(address),.mem_write_data(wdata),.mem_ready(ready),.mem_error(error),.mem_read_data(rdata),
        .console_active(stopped),.console_halt(),.wait_active(),.waiting(),.retire(retire),.psw(),.ir(),
        .mmr0(),.mmr1(),.mmr2(),.mmr3(),.upc(),.uword(),.pc(pc),
        .debug_register_data(regdata),.debug_register_address(5'd0));
    assign error=address>=22'h200000;
    wire [1:0] cpu_lanes=byte_access ? (address[0] ? 2'b10 : 2'b01) : 2'b11;
    reg rw=0;reg [2:0] ra=0;reg [15:0] wd=0;
    wire vr,vready;wire [19:0] va;
    uj11_pal_video video(.clk(clk),.video_clk(vclk),.reset(reset),.reg_write(rw),
        .reg_address(ra),.reg_data(wd),.reg_lanes(2'b11),.reg_read(),
        .dma_request(vr),.dma_address(va),.dma_ready(vready),.dma_data(rdata),.dac());
    wire mr,mw,mready;wire [19:0] ma;wire [1:0] ml;wire [15:0] md;
    generate if(MODE==0)begin: original
        assign vready=0;
        uj11_mmu_sram_arbiter arb(.clk(clk),.reset(reset),
            .cpu_request(request),.cpu_write(writing),.cpu_lock(cpu_lock),.cpu_address(address[20:1]),
            .cpu_lanes(cpu_lanes),.cpu_data(wdata),.cpu_ready(ready),
            .dma_request(1'b0),.dma_lanes(2'b11),.dma_write(1'b0),.dma_address(22'b0),.dma_data(16'b0),.dma_ready(),
            .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    end else begin: pal
        uj11_video_arbiter #(.FAST_TURNAROUND(1)) arb(.clk(clk),.reset(reset),
            .cpu_request(request),.cpu_write(writing),.cpu_lock(cpu_lock),.cpu_address(address[20:1]),
            .cpu_lanes(cpu_lanes),.cpu_data(wdata),.cpu_ready(ready),
            .dma_request(1'b0),.dma_lanes(2'b11),.dma_write(1'b0),.dma_address(22'b0),.dma_data(16'b0),.dma_ready(),
            .video_request(vr),.video_address(va),.video_ready(vready),
            .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    end endgenerate
    wire initialized;wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_sram #(.CLEAR_WORDS(1),.FAST_RESPONSE(MODE!=0)) memory(.clk(clk),.power_on(power_on),.reset(reset),
        .initialized(initialized),.request(mr),.write(mw),.address(ma),.byte_enable(ml),
        .write_data(md),.read_data(rdata),.ready(mready),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub));
    async_sram_model chip(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    integer pos,n,restart_pc,check_pc;
    integer instructions=0,video_words=0,batches=0,bus_wait=0;
    reg measuring=0;
    task word(input [15:0] v);
        begin chip.memory[pos]=v[7:0];chip.memory[pos+1]=v[15:8];pos=pos+2;end
    endtask
    task write_reg(input [2:0] a,input [15:0] v);
        begin @(negedge clk);ra=a;wd=v;rw=1;@(negedge clk);rw=0;end
    endtask
    always @(posedge clk)if(!reset)begin
        if(stopped || (request && ready && error))$fatal(1,"CPU trap PC=%o",pc);
        if(dut.step && dut.fetching && dut.read_a==check_pc)begin
            if(regdata!==(n==0 ? 16'o121060 : n==1 ? 16'o14315 : n==2 ? 16'd74 : 16'o12345))
                $fatal(1,"CPU result workload=%0d R0=%o",n,regdata);
            if(measuring)batches++;
        end
        if(measuring)begin
            if(retire)instructions++;
            if(vready)video_words++;
            if(dut.memory_op && !dut.memory_done && dut.mmu.state==5)bus_wait++;
        end
    end
    initial begin
        for(integer i=0;i<2097152;i++)chip.memory[i]=0;
        repeat(8)@(negedge clk);power_on=0;wait(initialized);
        for(n=0;n<4;n++)begin
            @(negedge clk);reset=1;repeat(8)@(negedge clk);
            pos='o4000;
            // Code/data in the same fully mapped 8 KiB kernel page.
            word('o12737);word('o77406);word('o172300);
            word('o12737);word(0);word('o172340);
            word('o12737);word(1);word('o177572);
            restart_pc=pos;
            word('o12704);word(n==2 ? 128 : 1000);
            word('o12700);word(n==2 ? 37 : 'o12345);
            word('o12701);word('o3210);
            word('o12702);word(n==2 ? 3 : 'o10000);
            chip.memory['o10000]=0;chip.memory['o10001]=0;
            if(n==3)begin
                word('o12703);word('o14000);
                for(integer i=0;i<1000;i++)begin
                    chip.memory['o10000+2*i]=(16'h5a5a ^ i)&255;
                    chip.memory['o10001+2*i]=(16'h5a5a ^ i)>>8;
                    chip.memory['o14000+2*i]=0;chip.memory['o14001+2*i]=0;
                end
            end
            if(n==0)begin
                word('o60100);word('o74001);word('o6001);word('o77404);
            end else if(n==1)begin
                word('o60012);word('o5200);word('o74001);word('o77404);
            end else if(n==2)begin
                word('o12700);word(37);word('o70002);word('o71002);word('o72027);word(1);word('o77407);
            end else begin word('o12223);word('o77402);end
            check_pc=pos;word(16'o400 | (((restart_pc-pos-2)/2)&255));
            reset=0;wait(!video.palette_init);
            write_reg(2,16'h1e);write_reg(0,MODE==2 ? 1 : MODE==3 ? 3 : 0);
            write_reg(4,1);wait(video.config_busy);wait(!video.config_busy);
            // Warm CPU caches and start at the next complete frame.
            @(posedge video.frame_start);@(negedge clk);
            instructions=0;video_words=0;batches=0;bus_wait=0;measuring=1;
            repeat(8000000)@(negedge clk);
            measuring=0;
            if(!dut.mmr0[0] || !batches || video.underruns)$fatal(1,"MMU/result/underrun");
            if(video_words!=(MODE>=2 ? 256000 : 0))$fatal(1,"Video traffic %0d",video_words);
            if(n==3)for(integer i=0;i<1000;i++)
                if({chip.memory['o14001+2*i],chip.memory['o14000+2*i]}!==(16'h5a5a ^ 16'(i)))
                    $fatal(1,"Copy mismatch %0d",i);
            $display("PAL_CPU_PERF {\"mode\":%0d,\"workload\":%0d,\"cycles\":8000000,\"instructions\":%0d,\"batches\":%0d,\"bus_wait\":%0d,\"video_words\":%0d}",
                MODE,n,instructions,batches,bus_wait,video_words);$fflush();
        end
        $display("PASS PAL CPU performance");$finish;
    end
    initial begin #1000000000;$fatal(1,"Benchmark timeout n=%0d",n);end
endmodule
