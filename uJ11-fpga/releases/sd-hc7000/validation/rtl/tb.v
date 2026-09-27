`timescale 1ns/1ps
// Full SD bootstrap -> RT-11 using actual CPU, SERV, SRAM and UART waveforms.
module tb_mmu_boot;
// Included by test_mmu_basic.py in the full CPU/SERV/SD/SRAM/UART harness.
// No register, memory or device-response substitution.
    integer ready_count=0,option_count=0;
    reg [47:0] basic_window=0;
    always @(posedge clk) if(dut.request && dut.ready && dut.writing &&
                            dut.address==22'o17777566)begin
        basic_window={basic_window[39:0],dut.write_data[7:0]};
        if(basic_window==48'h52454144590d)ready_count++;
        if(basic_window=="DUAL)?")option_count++;
    end
    task basic(input string command,input bit options=0);
        integer before_count;
        begin
            before_count=options ? option_count : ready_count;segment="";
            for(integer i=0;i<command.len();i++)send_byte(command[i]);send_byte(13);
            if(options)wait(option_count>before_count);else wait(ready_count>before_count);
            wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        end
    endtask
    task test_basic(input string name,input bit double_precision);
        begin
            basic({"RUN ",name},1);basic("A");
            basic("RUN B81TST");contains("CHECKS= 29  ERRORS= 0");contains("CP81 PASS");
            if(double_precision)begin basic("RUN B81DBL");contains("CP81 DOUBLE PASS");end
            basic("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT SQR(-1)");contains("?NEGATIVE SQUARE ROOT");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT 1E30*1E30");contains("?FLOATING OVERFLOW");
            basic("PRINT 2+2");contains(" 4 ");
            basic("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic("PRINT 2+2");contains(" 4 ");shell("BYE");
        end
    endtask

// Published image validation through the actual SD/SRAM/UART interfaces.
    integer timer_lines=0,high_writes=0,high_reads=0;
    always @(posedge clk) if(dut.request && dut.ready)begin
        if(dut.address>=22'h40000 && dut.address<22'h200000)begin
            if(dut.writing)high_writes++;else high_reads++;
        end
        if(dut.writing && dut.address==22'o17777566 && previous_char==10 && dut.write_data[7:0]=="T")
            timer_lines++;
    end
    task manual(input string name);
        begin shell({"TYPE ",name,".TXT"});contains({"END ",name,".TXT"});end
    endtask
    task test_sd_kit;
        integer oldlines,oldprompts;
        string timer_command;
        begin
            shell("SHOW CONFIGURATION");contains("2048KB of memory");contains("22 bit addressing is on");
            shell("SHOW MEMORY");contains("10000000  MEMTOP");
            manual("README");manual("BASIC");manual("ODT");manual("MEMORY");
            manual("TIMER");manual("HOST");manual("FILES");manual("BUILD");
            shell("LOAD HG");shell("UNLOAD HG");
            oldlines=timer_lines;oldprompts=prompts;segment="";
            timer_command="RUN TMRATE";
            for(integer i=0;i<timer_command.len();i++)send_byte(timer_command[i]);send_byte(13);
            wait(timer_lines>=oldlines+2);repeat(200000)@(negedge clk);
            contains("CFG ");send_byte("Q");wait(prompts>oldprompts);wait(serial_chars==bus_chars);
            shell("COPY README.TXT XMTEST.TXT");
            shell("DIFFERENCES/BINARY README.TXT XMTEST.TXT");contains("No differences found");
            shell("DELETE/NOQUERY XMTEST.TXT");
            shell("INITIALIZE/NOQUERY VM:");
            shell("COPY RT11XM.SYS VM:MMUTST.SYS");
            shell("DIFFERENCES/BINARY RT11XM.SYS VM:MMUTST.SYS");contains("No differences found");
            shell("UNPROTECT VM:MMUTST.SYS");shell("DELETE/NOQUERY VM:MMUTST.SYS");
        end
    endtask

    reg clk=0,power_on=1,rx=1,halt_button=0;
    always #20.833 clk=~clk;
    wire initialized,tx,boot_complete,stopped;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire cs,sck,mosi,miso;wire [7:0] pins;
    integer clocks=0,bus_chars=0,serial_chars=0,prompts=0,phase=0,checks=0;
    integer dma_words=0,concurrent_fetches=0,mmu_fetches=0,uart_file,b;
    reg [7:0] expected[0:65535],serial_value,previous_char=0;
    string segment="",uart_path,monitor;
    uj11_mmu_board dut(.clk(clk),.reset(power_on || !initialized),.power_on(power_on),
        .uart_rx(rx),.halt_button(halt_button),.memory_initialized(initialized),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete),.stopped(stopped));
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
            @(negedge clk);rx=0;repeat(208)@(negedge clk);
            for(integer bitno=0;bitno<8;bitno++)begin rx=value[bitno];repeat(208)@(negedge clk);end
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
                if(last_chars!=bus_chars || dut.bus.disk.rk_busy)unchanged=0;
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
            @(negedge tx);repeat(104)@(negedge clk);
            check(tx===0,"UART start bit");
            for(b=0;b<8;b++)begin repeat(208)@(negedge clk);serial_value[b]=tx;end
            repeat(208)@(negedge clk);
            check(tx===1 && serial_chars<bus_chars && serial_value===expected[serial_chars],"UART waveform");
            serial_chars++;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    always @(posedge clk) begin
        clocks<=clocks+1;
        if(initialized && !power_on)begin
            if(dut.bus.dma_request && dut.bus.dma_ready)dma_words++;
            if(dut.cpu.step && dut.cpu.fetching)begin
                if(dut.bus.disk.rk_busy)concurrent_fetches++;
                if(dut.cpu.mmr0[0])mmu_fetches++;
            end
            if(dut.request && dut.ready && dut.writing && dut.address==22'o17777566)begin
                expected[bus_chars]=dut.write_data[7:0];bus_chars++;
                segment={segment,dut.write_data[7:0]};
                if(previous_char==10 && dut.write_data[7:0]==".")prompts++;
                previous_char=dut.write_data[7:0];
            end
            if(stopped)$fatal(1,"unexpected console PC=%o IR=%o MMR0=%o MMR2=%o",dut.cpu.pc,dut.cpu.ir,dut.cpu.mmr0,dut.cpu.mmr2);
        end
        if(clocks>1200000000)$fatal(1,"timeout phase%0d PC=%o IR=%o uPC=%h",phase,dut.cpu.pc,dut.cpu.ir,dut.cpu.upc);
        if(clocks!=0 && clocks%20000000==0)begin
            $display("MMU boot progress %0d phase%0d PC=%o MMR0=%o DMA=%0d UART=%0d",clocks,phase,dut.cpu.pc,dut.cpu.mmr0,dma_words,bus_chars);$fflush();
        end
    end
    initial begin
        if(!$value$plusargs("MONITOR=%s",monitor))monitor="fb";
        repeat(5)@(negedge clk);power_on=0;
        settled_prompt();
        contains(monitor=="xm" ? "RT-11XM" : "RT-11FB");check(boot_complete,"bootstrap released");
        shell("SET SL OFF");
        phase=1;test_sd_kit();
        phase=2;test_basic("B81FPU",0);
        phase=3;test_basic("B81FPD",1);
        phase=4;shell("SHOW CONFIGURATION");contains("Booted from DM0:RT11XM");
        check(high_writes>27000 && high_reads>27000,"VM accesses extended RAM");
        check(mmu_fetches>1000 && dma_words>1000 && concurrent_fetches>100,"MMU and concurrent disk IO");
        $display("PASS MMU SD KIT: %0d checks, %0d clocks, %0d DMA words, %0d mapped fetches",checks,clocks,dma_words,mmu_fetches);$finish;
    end
endmodule
