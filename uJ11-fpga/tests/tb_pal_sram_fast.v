`timescale 1ns/1ps
// Four-state pin-delay test of the fast three-master handshake.
module tb_pal_sram_fast;
    reg clk=0,reset=1,power_on=1,cpu_lock=0;
    always #10 clk=~clk;
    reg [2:0] req=0,wr=0;
    reg [19:0] addr[0:2];reg [15:0] wd[0:2];reg [1:0] lanes=3;
    wire [2:0] ack;
    wire mr,mw,mready;wire [19:0] ma;wire [1:0] ml;wire [15:0] md,rd;
    uj11_video_arbiter #(.FAST_TURNAROUND(1)) arb(.clk(clk),.reset(reset),
        .cpu_request(req[0]),.cpu_write(wr[0]),.cpu_lock(cpu_lock),.cpu_address(addr[0]),
        .cpu_lanes(lanes),.cpu_data(wd[0]),.cpu_ready(ack[0]),
        .dma_request(req[1]),.dma_lanes(2'b11),.dma_write(wr[1]),.dma_address({1'b0,addr[1],1'b0}),
        .dma_data(wd[1]),.dma_ready(ack[1]),.video_request(req[2]),.video_address(addr[2]),.video_ready(ack[2]),
        .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    wire initialized;wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_sram #(.CLEAR_WORDS(1),.FAST_RESPONSE(1)) memory(.clk(clk),.reset(reset),.power_on(power_on),
        .request(mr),.write(mw),.address(ma),.byte_enable(ml),.write_data(md),.read_data(rd),
        .ready(mready),.initialized(initialized),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub));
    wire [19:0] pa;wire [15:0] pd;wire pce,poe,pwe,plb,pub;
    assign #11 pa=sa;
    assign #11 {pce,poe,pwe,plb,pub}={ce,oe,we,lb,ub};
    assign #16 pd=memory.drive_data ? memory.data_out : 16'bz;
    assign #1 sd=!memory.drive_data ? pd : 16'bz;
    async_sram_model chip(pa,pd,pce,poe,pwe,plb,pub);
    integer checks=0;
    realtime last_start=0,min_gap=1e9;
    always @(negedge pce)if(initialized && !reset)begin
        if(last_start && $realtime-last_start<min_gap)min_gap=$realtime-last_start;
        last_start=$realtime;
    end
    always @(posedge clk)if(!reset)begin
        if((ack & (ack-1'b1))!=0)$fatal(1,"More than one master acknowledged");
        if(cpu_lock && (ack[1] || ack[2]))$fatal(1,"DMA during CPU lock");
    end
    task automatic transfer(input integer client,input bit writing,input [19:0] a,
                            input [15:0] value,input integer hold_cycles);
        begin
            @(negedge clk);req[client]=1;wr[client]=writing;addr[client]=a;wd[client]=value;
            do @(posedge clk);while(!ack[client]);
            if(!writing && rd!==value)$fatal(1,"Client %0d read %h at %h expected %h",client,rd,a,value);
            checks++;
            repeat(hold_cycles)@(posedge clk);
            @(negedge clk);req[client]=0;
        end
    endtask
    initial begin
        for(integer i=0;i<3;i++)begin addr[i]=0;wd[i]=0;end
        for(integer i=0;i<4096;i++)chip.memory[i]=0;
        for(integer i=0;i<200;i++)begin chip.memory[2*('h300+i)]=i;chip.memory[2*('h300+i)+1]='ha5;end
        repeat(8)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        fork
            begin
                for(integer i=0;i<200;i++)begin
                    lanes=3;transfer(0,1,'h100+i,16'hc000+i,i%4);
                    transfer(0,0,'h100+i,16'hc000+i,0);
                    lanes=1;transfer(0,1,'h100+i,16'hff00+(199-i),0);
                    lanes=3;transfer(0,0,'h100+i,16'hc000+(199-i),i%3);
                end
            end
            begin
                for(integer i=0;i<200;i++)begin
                    transfer(1,1,'h200+i,16'hd000+i,i%3);
                    transfer(1,0,'h200+i,16'hd000+i,0);
                end
            end
            begin
                for(integer i=0;i<600;i++)transfer(2,0,'h300+(i%200),16'ha500+(i%200),i%2);
            end
        join
        repeat(12)@(negedge clk);
        // Withdrawing a video request before/after SRAM completion must not
        // forward that response to another owner, even if its address changes.
        for(integer phase=0;phase<7;phase++)begin
            @(negedge clk);addr[2]='h300;req[2]=1;
            wait(arb.state==3);repeat(phase)@(negedge clk);
            @(negedge clk);req[2]=0;addr[2]=20'hfffff;
            transfer(0,0,'h100,16'hc0c7,0);
            transfer(1,0,'h200,16'hd000,0);
            repeat(8)@(negedge clk);
        end
        cpu_lock=1;
        fork
            begin repeat(12)transfer(0,0,'h100,16'hc0c7,0);@(negedge clk);cpu_lock=0;end
            transfer(1,0,'h200,16'hd000,0);
            transfer(2,0,'h300,16'ha500,0);
        join
        repeat(12)@(negedge clk);
        // Peripheral reset at every transaction phase; preserve unrelated RAM.
        for(integer phase=0;phase<7;phase++)begin
            @(negedge clk);wr[1]=1;wd[1]=16'hdead;addr[1]='h400;req[1]=1;
            wait(arb.state==2);repeat(phase)@(negedge clk);
            @(negedge clk);reset=1;req=0;
            repeat(2)@(negedge clk);reset=0;repeat(8)@(negedge clk);
            if(ack)$fatal(1,"Stale completion after reset");
            transfer(0,0,'h100,16'hc0c7,0);
            transfer(2,0,'h300,16'ha500,0);
        end
        if(min_gap!=120)$fatal(1,"Expected six-clock saturated SRAM interval: %0f ns",min_gap);
        $display("Fast SRAM minimum transaction interval: %0f ns",min_gap);
        $display("PASS fast SRAM arbiter: %0d checks, mixed read/write, byte lanes, held requests, cancellation, LOCK, reset, pin delays",checks);$finish;
    end
    initial begin #10000000;$fatal(1,"Fast SRAM test timeout");end
endmodule
