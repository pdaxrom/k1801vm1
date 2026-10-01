`timescale 1ns/1ps
// Real SERV, PAL, asynchronous SRAM and serial wire. No simulated text renderer.
module tb_terminal;
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;
    reg [15:0] write_data=0;
    wire ready,error,initialized,tx,cpu_start;
    wire [15:0] read_data;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_mmu_board_bus #(.CLOCK_HZ(50000000),.CLEAR_WORDS(1),.BOOT_ROM_ENABLE(0),
        .VIDEO_ENABLE(1),.TERMINAL_ENABLE(1)) bus(
        .clk(clk),.video_clk(vclk),.video_reset(1'b0),.reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.dma_map_enabled(1'b0),.tvout(),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(1'b0),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(),.irq_priority(),.irq_vector(),.irq_ack(1'b0),.uart_rx(1'b1),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),
        .sram_lb_n(lb),.sram_ub_n(ub),.sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),
        .boot_complete(),.cpu_start(cpu_start));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    serv_memory_guard guard(.clk(clk),.reset(bus.disk.iop_reset),
        .write(bus.disk.data_accept && bus.disk.memory_selected && bus.disk.de),.address(bus.disk.da));
    integer accepted=0,received=0,stalls=0,checks=0;
    reg [7:0] expected[0:4095],serial;
    always @(posedge clk)begin
        if(bus.mirror_push && bus.disk.terminal.fifo.enabled)begin
            expected[accepted]=write_data[7:0];accepted++;
        end
        if(request && writing && bus.uart_selected && !bus.mirror_ready)stalls++;
        if(bus.disk.dma_request && bus.disk.dma_reserved && bus.disk.dma_address<'h1e4000)
            $fatal(1,"SERV escaped its reservation");
    end
    initial forever begin
        @(negedge tx);repeat(217)@(negedge clk);
        if(tx)$fatal(1,"UART start bit");
        for(integer b=0;b<8;b++)begin repeat(434)@(negedge clk);serial[b]=tx;end
        repeat(434)@(negedge clk);
        if(!tx || received>=accepted || serial!=expected[received])$fatal(1,"UART byte %0d",received);
        received++;checks++;
    end
    task access(input [21:0] a,input bit w,input [15:0] data,input bit byte_mode,input bit nxm);
        @(negedge clk);address=a;writing=w;write_data=data;byte_access=byte_mode;request=1;
        do @(negedge clk);while(!ready);
        if(error!=nxm)$fatal(1,"bus NXM %h error %b",a,error);
        checks++;request=0;
        repeat(2)@(negedge clk);
    endtask
    task send(input [7:0] ch);
        begin
            do access(22'o17777564,0,0,0,0);while(!read_data[7]);
            access(22'o17777566,1,{8'b0,ch},1,0);
        end
    endtask
    task text(input string s);
        for(integer i=0;i<s.len();i++)send(s[i]);
    endtask
    task rendered;
        // Give the real bit-serial renderer time to drain its deferred work.
        repeat(40000000)@(negedge clk);
    endtask
    string out;integer f;
    initial begin
        for(integer i=0;i<2097152;i++)ram.memory[i]=8'h5a;
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait(cpu_start);
        if(bus.pal.video.HEIGHT!=240 || bus.pal.video.active_base!=20'hf6000)
            $fatal(1,"PAL must be active before CPU start");
        for(integer i='h1ec000;i<'h1fec00;i++)begin
            if(ram.memory[i]!=0)$fatal(1,"framebuffer clear %h",i);
            checks++;
        end
        access(22'h1e3ffe,1,16'hcafe,0,0);
        access(22'h1e4000,1,16'hbad,0,1);
        access(22'h1ffffe,0,0,0,1);
        // High byte TBUF writes must not create a mirrored or serial character.
        access(22'o17777567,1,16'hffff,1,0);
        text("uJ11 PAL CONSOLE\r\nBOOT MENU / ODT / RT-11 / RSX / BSD\r\n");
        text("\033[5;10H\033[31;44mRED ON BLUE\033[0m\r\n");
        text("\033[?2l\033Y(*VT52\033<\033[?25l");
        rendered();
        wait(received==accepted);@(negedge clk);peripheral_reset=1;
        repeat(5)@(negedge clk);peripheral_reset=0;
        repeat(10000)@(negedge clk);
        if(bus.pal.video.vcontrol!=1)$fatal(1,"guest RESET turned off terminal");
        if(ram.memory['h1e4000]!="u" || ram.memory['h1e4140]!="B")$fatal(1,"console text copy");
        if(ram.memory['h1e4000+4*(4*80+9)]!="R")$fatal(1,"ANSI cursor/colour");
        if(ram.memory['h1e4000+4*(8*80+10)]!="V")$fatal(1,"VT52 positioning");
        if($value$plusargs("OUT=%s",out))begin
            f=$fopen({out,"/frame.bin"},"wb");
            for(integer i='h1ec000;i<'h1fec00;i++)$fwrite(f,"%c",ram.memory[i]);
            $fclose(f);
        end
        text("\033[2J\033[H");
        for(integer line=0;line<35;line++)text($sformatf("LINE %02d\r\n",line));
        rendered();wait(received==accepted);
        if(bus.disk.terminal.fifo.count!=0)$fatal(1,"undrained console queue");
        if(bus.pal.video.vorigin!=48)$fatal(1,"ring scroll origin %0d",bus.pal.video.vorigin);
        if(bus.pal.video.underruns)$fatal(1,"PAL underrun %0d",bus.pal.video.underruns);
        $display("PASS PAL console mirror: %0d checks, %0d serial bytes, %0d backpressure clocks, 240 rows",checks,received,stalls);
        $finish;
    end
    initial begin #5000000000;$fatal(1,"mirror timeout PC=%h",bus.disk.ia);end
endmodule
