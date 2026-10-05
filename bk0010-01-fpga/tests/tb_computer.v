`timescale 1ns/1ps
// Complete native SD cold boot through the physical SRAM and PS/2 pins.
module tb_computer;
    reg clk = 0, clk48 = 0, video_clk = 0, power_on = 1, button_n = 1;
    always #20.833333 clk = !clk;
    always #10.416667 clk48 = !clk48;
    always #7.8125 video_clk = !video_clk;
    reg ps2_low = 0, ps2_data = 1;
    tri1 ps2_clock;
    assign ps2_clock = ps2_low ? 1'b0 : 1'bz;
    wire [19:0] sram_address;
    wire [15:0] sram_data;
    wire ce, oe, we, lb, ub, sd_cs, sd_sck, sd_mosi, sd_miso;
    wire [5:0] tvout;
    wire [1:0] audio;
    wire tx, initialized, locked, error, underrun;
    wire [7:0] stage;
    wire [8:0] high_digit, low_digit;
    integer reads, polls = 0, screen_writes = 0, cycles = 0, previous_reads;
    reg [7:0] previous_stage = 0;
    // Accelerate only the physical button debounce/display timebase.
    // CPU, SRAM, PAL, SPI, UART and PS/2 clocks retain their hardware rates.
    bk_computer #(.PANEL_CLOCK_HZ(32000)) computer(
        clk,clk48,video_clk,power_on,button_n,ps2_clock,ps2_data,
        sram_address,sram_data,ce,oe,we,lb,ub,sd_cs,sd_sck,sd_mosi,sd_miso,
        tvout,audio,tx,high_digit,low_digit,initialized,locked,error,underrun,stage);
    sram_model ram(sram_address,sram_data,ce,oe,we,lb,ub);
    sd_spi_model card(sd_cs,sd_sck,sd_mosi,sd_miso,reads);
    always @(posedge clk) begin
        cycles <= cycles + 1;
        if(stage != previous_stage) begin
            $display("COMPUTER stage=%02h sys_cycles=%0d reads=%0d",stage,cycles,reads);
            $fflush();
            previous_stage <= stage;
        end
        if(error) $fatal(1,"Full computer boot error=%02h",stage);
        if(underrun) $fatal(1,"PAL underrun with running VM1 and SRAM");
        if(computer.cpu_mem_request && computer.cpu_mem_ready) begin
            if(computer.cpu_mem_write && computer.cpu_mem_address >= 8192 && computer.cpu_mem_address < 16384)
                screen_writes <= screen_writes + 1;
        end
        if(computer.external_request && computer.external_ready && !computer.external_write_enable &&
           computer.external_address == 16'o105 && locked && screen_writes > 1000) polls <= polls + 1;
    end
    task short_press;
        begin
            @(negedge clk); button_n = 0;
            repeat(3200) @(negedge clk);
            button_n = 1; repeat(2560) @(negedge clk);
            wait(computer.active_mode == computer.requested_mode);
        end
    endtask
    task bit_send(input value);
        begin ps2_data = value; #15000; ps2_low = 1; #30000; ps2_low = 0; #15000; end
    endtask
    task scan(input [7:0] value);
        integer bit_number;
        begin
            wait(ps2_clock); #10000; bit_send(0);
            for(bit_number=0;bit_number<8;bit_number=bit_number+1) bit_send(value[bit_number]);
            bit_send(~^value); bit_send(1); ps2_data = 1; #50000;
        end
    endtask
    task key(input [7:0] value);
        begin
            wait(!computer.bus.key_pending && !computer.key_valid);
            scan(value); scan(8'hf0); scan(value);
            wait(!computer.bus.key_pending && !computer.key_valid);
            repeat(100000) @(negedge clk);
        end
    endtask
    integer i;
    initial begin
        for(i=0;i<32768;i=i+1) ram.memory[i] = 16'ha5a5;
        #300; power_on = 0;
        wait(!computer.hard_reset);
        short_press(); short_press(); short_press();
        if(computer.active_mode != 3) $fatal(1,"Startup frequency");
        wait(polls == 30);
        $writememh("build/computer-boot-vram.hex",ram.memory,8192,16383);
        $display("PASS full computer cold boot: 24 MHz, SD sectors=%0d",reads);
        previous_reads = reads;
        button_n = 0; wait(computer.hard_reset);
        repeat(1000) @(negedge clk); button_n = 1;
        wait(!computer.hard_reset); polls = 0;
        wait(polls == 30);
        if(!locked || reads != previous_reads || computer.active_mode != 3) $fatal(1,"Warm reset lost ROM/frequency");
        $display("PASS full computer long-button warm reset: retained ROM and 24 MHz");
        scan(8'h12); // Shift for PRINT.
        short_press(); key(8'h4d);
        short_press(); key(8'h2d);
        short_press(); key(8'h43);
        short_press(); key(8'h31); key(8'h2c);
        scan(8'hf0); scan(8'h12);
        key(8'h29); key(8'h1e);
        scan(8'h12); key(8'h55); scan(8'hf0); scan(8'h12);
        key(8'h1e); key(8'h5a);
        polls = 0; wait(polls == 100);
        repeat(500000) @(negedge clk);
        $writememh("build/computer-print-vram.hex",ram.memory,8192,16383);
        $writememh("build/computer-rom.hex",ram.memory,16384,32767);
        if(reads != previous_reads || !locked || underrun) $fatal(1,"Runtime invariants");
        $display("PASS full computer physical PS/2 PRINT 2+2, live 3/6/12/24 MHz, PAL without underruns");
        $finish;
    end
    initial begin #10000000000; $fatal(1,"Computer timeout stage=%h addr=%06o ir=%06o",stage,
                                      computer.cpu_address,computer.cpu.core.ir); end
endmodule
