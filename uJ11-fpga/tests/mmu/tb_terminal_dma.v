`timescale 1ns/1ps
// Real SERV disk DMA at the boundary of guest RAM and the PAL reserve.
module tb_terminal_dma;
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;
    reg [15:0] write_data=0;
    wire ready,error,initialized,tx,cpu_start,irq;
    wire [8:0] vector;wire cs,sck,mosi,miso;
    wire [15:0] read_data;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_mmu_board_bus #(.CLOCK_HZ(50000000),.CLEAR_WORDS(1),.BOOT_ROM_ENABLE(0),
        .VIDEO_ENABLE(1),.TERMINAL_ENABLE(1),.SD_SLOW_DIV(125)) bus(
        .clk(clk),.video_clk(vclk),.video_reset(1'b0),.reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.dma_map_enabled(1'b0),.tvout(),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(1'b0),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(irq),.irq_priority(),.irq_vector(vector),.irq_ack(1'b0),.uart_rx(1'b1),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),
        .sram_lb_n(lb),.sram_ub_n(ub),.sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),
        .boot_complete(),.cpu_start(cpu_start));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    serv_memory_guard guard(.clk(clk),.reset(bus.disk.iop_reset),
        .write(bus.disk.data_accept && bus.disk.memory_selected && bus.disk.de),.address(bus.disk.da));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    integer checks=0,guest_dma=0;longint clocks=0,started;
    always @(posedge clk)begin
        clocks++;
        if(bus.dma_request && !bus.dma_reserved && !bus.dma_nxm && bus.dma_ready)guest_dma++;
    end
    task check(input bit ok,input string why);
        if(!ok)$fatal(1,"%s, CSR=%o SERV PC=%h",why,read_data,bus.disk.ia);else checks++;
    endtask
    task access(input [21:0] a,input bit w,input [15:0] data);
        @(negedge clk);address=a;writing=w;write_data=data;request=1;
        do @(negedge clk);while(!ready);
        check(!error,"IO response");request=0;repeat(2)@(negedge clk);
    endtask
    task put(input integer regno,input [15:0] data);
        access(22'o17774400+2*regno,1,data);
    endtask
    task done;
        do access(22'o17774400,0,0);while(!read_data[7]);
    endtask
    task transfer(input [15:0] command,input [21:0] destination);
        put(1,destination[15:0]);put(2,0);put(3,-2);put(4,destination[21:16]);
        put(0,command|((destination>>12)&16'o60));done();
    endtask
    function [15:0] word_at(input integer a);word_at={ram.memory[a+1],ram.memory[a]};endfunction
    integer before_dma,before_writes;
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;wait(cpu_start);
        do access(22'o17777504,0,0);while(!read_data[15]);
        check(!read_data[14],"SD initialized");
        // A transfer straddling the boundary writes only its first word.
        before_dma=guest_dma;transfer(16'o14,22'h1e3ffe);
        check((read_data&16'o176000)==16'o120000,"RL reports NXM at reserve");
        check(guest_dma==before_dma+1,"exactly one valid DMA word before boundary");
        check(word_at('h1e3ffe)==16'h0300,"last guest word transferred");
        check(word_at('h1e4000)==16'h0f20,"text screen protected");
        check(word_at('h1ec000)==0,"framebuffer protected");
        // RL commits MP/BA at the end of a complete sector; NXM leaves this
        // short sector's register values unchanged.
        access(22'o17774406,0,0);check(read_data==16'hfffe,"RL residual word count");
        before_dma=guest_dma;transfer(16'o14,22'h1ec000);
        check((read_data&16'o176000)==16'o120000 && guest_dma==before_dma,"DMA wholly inside reserve rejected");
        before_dma=guest_dma;before_writes=card.writes;transfer(16'o12,22'h1e3ffe);
        check((read_data&16'o176000)==16'o120000,"DMA read of reserve reports NXM");
        if(guest_dma!=before_dma+1 || card.writes!=before_writes)
            $display("DMA %0d -> %0d, SD writes %0d -> %0d",before_dma,guest_dma,before_writes,card.writes);
        check(guest_dma==before_dma+1 && card.writes==before_writes,"failed disk write leaves SD unchanged");
        put(0,16'o100);started=clocks;
        wait(irq);check(vector==9'o160,"RL probe interrupt vector");
        check(clocks-started<50000,"RL NOP completes within one millisecond");
        check(bus.pal.video.vcontrol==1 && !bus.pal.video.underruns,"PAL remains active");
        $display("PASS terminal DMA: %0d checks, reserve crossing/read/write and RL probe latency %0d clocks",checks,clocks-started);
        $finish;
    end
    initial begin #3000000000;$fatal(1,"terminal DMA timeout");end
endmodule
