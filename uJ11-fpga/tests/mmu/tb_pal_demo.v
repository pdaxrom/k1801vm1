`timescale 1ns/1ps
module tb_pal_demo;
    reg clk=0,vclk=0,reset=1,power_on=1,rx=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    wire request,writing,byte_access,cpu_lock,ready,error,initialized,irq_valid,irq_ack,peripheral_reset;
    wire [21:0] address;wire [15:0] write_data,read_data,irq_vector,mmr3,pc;
    wire [2:0] irq_priority;
    wire stopped,console_halt,cpu_start;
    uj11_mmu_cpu #(.BOOT_PC(16'o20000)) cpu(.clk(clk),.reset(reset || !cpu_start),.halt_button(1'b0),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .peripheral_reset(peripheral_reset),.mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_lock(cpu_lock),
        .mem_address(address),.mem_write_data(write_data),.mem_ready(ready),.mem_error(error),.mem_read_data(read_data),
        .console_active(stopped),.console_halt(console_halt),.wait_active(),.waiting(),.retire(),.psw(),.ir(),
        .mmr0(),.mmr1(),.mmr2(),.mmr3(mmr3),.upc(),.uword(),.pc(pc),.debug_register_data(),.debug_register_address(5'd0));
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_mmu_board_bus #(.CLOCK_HZ(50000000),.CLEAR_WORDS(1),.VIDEO_ENABLE(1),.BOOT_ROM_ENABLE(0)) bus(
        .ps2_clock(),.ps2_data(1'b1),
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),
        .dma_map_enabled(mmr3[5]),.video_clk(vclk),.video_reset(1'b0),.tvout(),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(cpu_lock),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),.uart_rx(rx),.uart_tx(),
        .panel_keys(4'b0),.panel_pins(),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.cpu_start(cpu_start));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    serv_memory_guard guard(.clk(clk),.reset(reset),
        .write(bus.disk.data_accept && bus.disk.memory_selected && bus.disk.de),.address(bus.disk.da));
    string output_path,uart="";integer file,bytes,phase=0;
    reg uart_seen=0;
    always @(posedge clk)begin
        if(!request)uart_seen<=0;
        if(!uart_seen && request && ready && writing && address==22'o17777566)begin
            uart={uart,write_data[7:0]};uart_seen<=1;$write("%c",write_data[7:0]);
        end
        if(stopped && phase!=4)$fatal(1,"Demo trapped/ halted PC=%o IR=%o MMR0=%o",pc,cpu.ir,cpu.mmr0);
    end
    task send(input [7:0] c);
        @(negedge clk);rx=0;repeat(434)@(negedge clk);
        for(integer b=0;b<8;b++)begin rx=c[b];repeat(434)@(negedge clk);end
        rx=1;repeat(500)@(negedge clk);
    endtask
    task dump(input string name);
        file=$fopen({output_path,"/",name},"wb");
        if(!file)$fatal(1,"dump open");
        for(integer a='h1e0000;a<'h200000;a++)$fwrite(file,"%c",ram.memory[a]);
        $fclose(file);
    endtask
    initial begin
        if(!$value$plusargs("OUT=%s",output_path))$fatal(1,"OUT required");
        for(integer a=0;a<2097152;a++)ram.memory[a]=0;
        $readmemh("build/pal-demo/paldem.bytes",ram.memory,'o20000);
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait(bus.pal.video.vcontrol==1 && !bus.pal.video.config_busy);
        dump("640.bin");
        // Wait for main to finish the UART banner and start polling its input.
        wait(uart.len()>=55);repeat(100000)@(posedge clk);
        phase=1;send("S");wait(bus.pal.video.vorigin==8 && !bus.pal.video.config_busy);
        dump("scroll.bin");
        send("P");wait(bus.pal.video.active_base==20'hf8000 && !bus.pal.video.config_busy);
        phase=2;send("2");wait(bus.pal.video.vcontrol==0);
        wait(bus.pal.video.vcontrol==3 && !bus.pal.video.config_busy);dump("320.bin");
        phase=3;send("B");wait(bus.pal.video.vcontrol==5 && !bus.pal.video.config_busy);
        if(bus.pal.video.dma_request)$fatal(1,"test pattern DMA");
        phase=4;send("Q");wait(stopped && console_halt);
        if(bus.pal.video.vcontrol || cpu.mmr0[0])$fatal(1,"demo exit did not disable video/MMU");
        if(bus.pal.video.underruns)$fatal(1,"demo underrun");
        $display("\nPASS PAL PDP-11 demo: text 80/40 columns, two pages, ring scroll, pattern and HALT");$finish;
    end
    initial begin #2000000000;$fatal(1,"PAL demo timeout phase=%0d PC=%o",phase,pc);end
endmodule
