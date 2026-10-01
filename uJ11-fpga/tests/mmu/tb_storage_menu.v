`timescale 1ns/1ps
// Full SD bootstrap -> RT-11 using actual CPU, SERV, SRAM and UART waveforms.
module tb_storage_menu #(parameter integer CLOCK_HZ=24000000);
    reg clk=0,power_on=1,hard_reset=0,rx=1,halt_button=0;
    always #(500000000.0/CLOCK_HZ) clk=~clk;
    localparam integer UART_BIT=CLOCK_HZ/115200;
    wire initialized,tx,boot_complete,stopped;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire cs,sck,mosi,miso;wire [7:0] pins;
    integer clocks=0,bus_chars=0,serial_chars=0,prompts=0,phase=0,checks=0;
    integer dma_words=0,concurrent_fetches=0,mmu_fetches=0,uart_file,b;
    reg [7:0] expected[0:65535],serial_value,previous_char=0;
    string segment="",uart_path,mode;
    integer timer_clears=0;
    reg [15:0] signature;
    // Accelerate only the 50 Hz timer by 100x, retaining real CPU/UART/SPI clocks.
    uj11_mmu_board #(.CLOCK_HZ(CLOCK_HZ),.TICK_DIVISOR(CLOCK_HZ/5000),
        .SD_SLOW_DIV((CLOCK_HZ+399999)/400000)) dut(
        .video_clk(1'b0),.video_reset(1'b1),.tvout(),.clk(clk),.reset(power_on || hard_reset || !initialized),.power_on(power_on),
        .uart_rx(rx),.halt_button(halt_button),.memory_initialized(initialized),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete),.stopped(stopped),
        .diagnostic_halt(),.diagnostic_wait(),.diagnostic_retire(),.diagnostic_mode());
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"phase %0d: %s",phase,why);checks++;end
    endtask
    task contains(input string wanted);
        bit found;
        begin
            found=0;
            for(integer i=0;i+wanted.len()<=segment.len();i++)
                if(segment.substr(i,i+wanted.len()-1)==wanted)found=1;
            check(found,{"expected ",wanted," in ",segment});
        end
    endtask
    task send_byte(input [7:0] value);
        begin
            @(negedge clk);rx=0;repeat(UART_BIT)@(negedge clk);
            for(integer bitno=0;bitno<8;bitno++)begin rx=value[bitno];repeat(UART_BIT)@(negedge clk);end
            rx=1;repeat(20000)@(negedge clk);
        end
    endtask
    task shell(input string command);
        integer oldprompt;
        begin
            oldprompt=prompts;segment="";
            for(integer i=0;i<command.len();i++)send_byte(command[i]);send_byte(13);
            wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(1000)@(negedge clk);
        end
    endtask
    task settled_prompt;
        integer unchanged,last_chars;
        begin
            wait(prompts>0);unchanged=0;last_chars=bus_chars;
            while(unchanged<2000000)begin
                @(negedge clk);
                if(last_chars!=bus_chars || dut.bus.disk.pending)unchanged=0;
                else unchanged++;
                last_chars=bus_chars;
            end
            wait(serial_chars==bus_chars);
        end
    endtask
    initial begin
        if(!$value$plusargs("UART_LOG=%s",uart_path))$fatal(1,"UART_LOG required");
        uart_file=$fopen(uart_path,"w");
        forever begin
            @(negedge tx);repeat(UART_BIT/2)@(negedge clk);
            check(tx===0,"UART start bit");
            for(b=0;b<8;b++)begin repeat(UART_BIT)@(negedge clk);serial_value[b]=tx;end
            repeat(UART_BIT)@(negedge clk);
            check(tx===1 && serial_chars<bus_chars && serial_value===expected[serial_chars],"UART waveform");
            serial_chars++;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    always @(posedge clk) begin
        clocks<=clocks+1;
        if(initialized && !power_on && !hard_reset && !dut.cpu_start)begin
            if(dut.request || dut.cpu.retire)$fatal(1,"J11 ran before SERV installed bootstrap");
            if(dut.bus.dma_request && (dut.bus.dma_address<22'o4000 || dut.bus.dma_address>=22'o4000+430 || !dut.bus.dma_write))
                $fatal(1,"bootstrap DMA escaped its destination");
        end
        if(initialized && !power_on)begin
            if(dut.bus.dma_request && dut.bus.dma_ready)dma_words++;
            if(dut.cpu.step && dut.cpu.fetching)begin
                if(dut.bus.disk.pending)concurrent_fetches++;
                if(dut.cpu.mmr0[0])mmu_fetches++;
            end
            if(dut.request && dut.ready && dut.writing && dut.address==22'o17777566)begin
                expected[bus_chars]=dut.write_data[7:0];bus_chars++;
                segment={segment,dut.write_data[7:0]};
                if(previous_char==10 && dut.write_data[7:0]==".")prompts++;
                previous_char=dut.write_data[7:0];
            end
            if(dut.request && dut.ready && dut.writing && dut.address==22'o17777546)
                timer_clears++;
        end
        if(clocks>CLOCK_HZ*4)$fatal(1,"timeout phase%0d PC=%o IR=%o uPC=%h",phase,dut.cpu.pc,dut.cpu.ir,dut.cpu.upc);
        if(clocks!=0 && clocks%20000000==0)begin
            $display("MMU boot progress %0d phase%0d PC=%o MMR0=%o DMA=%0d UART=%0d",clocks,phase,dut.cpu.pc,dut.cpu.mmr0,dma_words,bus_chars);$display("BOOT=%b ABI=%o/%o/%o ticks=%0d",boot_complete,word_at('o1000),word_at('o1002),word_at('o1004),timer_clears);$fflush();
        end
    end
    function bit has_text(input string wanted);
        has_text=0;
        for(integer i=0;i+wanted.len()<=segment.len();i++)
            if(segment.substr(i,i+wanted.len()-1)==wanted)has_text=1;
    endfunction
    task until_text(input string wanted);
        while(!has_text(wanted))repeat(500)@(negedge clk);
    endtask
    function [15:0] word_at(input integer a);word_at={ram.memory[a+1],ram.memory[a]};endfunction
    reg [15:0] bootstrap[0:511];
    initial $readmemh("build/hc7000-mmu-hardware/bootstrap.mem",bootstrap);
    always @(posedge dut.cpu_start)if(!power_on)begin
        for(integer i=0;i<215;i++)check(word_at('o4000+2*i)===bootstrap[i],"complete bootstrap before CPU release");
    end
    initial begin
        if(!$value$plusargs("MODE=%s",mode))mode="auto";
        signature=mode=="cancel" ? 'o12345 : mode=="rl-auto" ? 'o12346 : mode=="rq-auto" ? 'o12350 : 'o12354;
        repeat(5)@(negedge clk);power_on=0;
        if(mode=="bad-wire")begin
            wait(dut.bus.iop_selected && dut.ready && dut.bus.offset==13'o17504 && dut.bus.io_data[15]);card.corrupt_read_crc=1;
        end
        if(mode=="bad-header" || mode=="bad-payload" || mode=="bad-size" || mode=="bad-wire")begin
            wait(stopped);
            check(word_at('o157774)==(mode=="bad-wire" ? 'o12 : 'o11),"menu corruption diagnostic");
            check(!boot_complete && card.writes==0 && cs,"corrupt menu cannot boot or write SD");
        end else begin
            if(mode!="direct" && mode!="reset")begin
                until_text("uJ11 SD BOOT MENU");
                until_text(mode=="rl-auto" ? "RL11, units: 0 1" : mode=="xp-auto" ? "XP/RP, units: 0 7" : mode=="rk-auto" ? "RK05, units: 0 7" : mode=="rq-auto" ? "RQ/MSCP, units: 0 3" : "RH11/HK, units: 0 7");
            end
            if(mode=="cancel" || mode=="enter")begin
                until_text("other key=menu: ");
                send_byte(mode=="enter" ? 13 : "M");
            end
            if(mode=="cancel" || mode=="no-default")begin
                until_text("Select controller");
                repeat(1500000)@(negedge clk);
                check(!boot_complete,"manual selection does not time out");
                send_byte("1");until_text("Controller not present");
                send_byte("2");until_text("Select unit");
                send_byte("1");until_text("Unit not present");
                send_byte(mode=="cancel" ? "0" : "7");
            end
            wait(boot_complete);
            while(word_at('o1002)!=signature)@(negedge clk);
            check(word_at('o1000)==(mode=="cancel" ? 0 : mode=="rl-auto" ? 1 : mode=="rq-auto" ? 3 : 7),"selected unit in boot ABI");
            // RK bootstrap passes RKCS (base+4), as the standard RK05 ABI does.
            while(word_at('o1004)!=(mode=="rl-auto" ? 'o174400 : mode=="xp-auto" ? 'o176700 : mode=="rk-auto" ? 'o177404 : mode=="rq-auto" ? 'o172150 : 'o177440))@(negedge clk);
            check(card.writes==0,"menu never writes SD");
            if(mode=="auto" || mode=="rl-auto" || mode=="xp-auto" || mode=="rk-auto" || mode=="rq-auto")check(timer_clears==251,"five seconds use 250 timer ticks");
            if(mode=="enter")check(timer_clears<251,"Enter boots before timeout");
            if(mode=="no-default" || mode=="direct" || mode=="reset")check(timer_clears==0,"no unwanted timeout");
            if(mode=="reset")begin
                phase=1;
                // Simulate an OS reusing bootstrap RAM, then execute guest RESET.
                ram.memory['o4000]=8'h5a;ram.memory['o4001]=8'ha5;
                ram.memory[16]=8'hff;ram.memory[17]=8'h01; // BR . after RESET
                ram.memory[14]=5;ram.memory[15]=0;
                wait(dut.peripheral_reset);repeat(1000)@(negedge clk);
                check(dut.cpu_start && boot_complete && word_at('o4000)==16'ha55a,
                    "guest RESET does not restart SERV or overwrite guest memory");
                phase=2;
                // A board RESET must reinstall the bootstrap in unchanged SRAM.
                ram.memory['o1002]=0;ram.memory['o1003]=0;
                @(negedge clk);hard_reset=1;repeat(8)@(negedge clk);
                check(!dut.cpu_start,"board RESET closes CPU release latch");
                hard_reset=0;wait(dut.cpu_start);wait(boot_complete);
                while(word_at('o1002)!=signature)@(negedge clk);
                check(word_at('o1000)==7 && card.writes==0,"warm boot restores selected unit without SD writes");
            end
        end
        wait(serial_chars==bus_chars);
        $display("PASS MMU storage menu %s: %0d checks, %0d timer clears, %0d clocks",mode,checks,timer_clears,clocks);$finish;
    end
endmodule
