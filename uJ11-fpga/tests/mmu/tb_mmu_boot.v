`timescale 1ns/1ps
// Full SD bootstrap -> RT-11 using actual CPU, SERV, SRAM and UART waveforms.
module tb_mmu_boot;
    reg clk=0,power_on=1,rx=1,halt_button=0;
    always #20.833 clk=~clk;
    wire initialized,tx,boot_complete,stopped;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire cs,sck,mosi,miso;wire [7:0] pins;
    integer max_clocks=600000000;
    initial if($test$plusargs("BOOT_MENU"))max_clocks=800000000;
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
                if(dut.bus.disk.pending)concurrent_fetches++;
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
        if(clocks>max_clocks)$fatal(1,"timeout phase%0d PC=%o IR=%o uPC=%h",phase,dut.cpu.pc,dut.cpu.ir,dut.cpu.upc);
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
        phase=1;shell("DIR RT11*.SYS");contains("RT11XM");contains("RT11FB");
        phase=2;if(monitor!="xm")
        begin
            shell("BOOT RT11XM");settled_prompt();
            contains("RT-11XM");
            shell("SET SL OFF");
        end
        phase=3;shell("SHOW MEMORY");
        check(mmu_fetches>1000,"XM executes with MMU enabled");
        phase=4;shell("DIR RT11*.SYS");contains("RT11XM");
        check(dma_words>1000 && concurrent_fetches>100,"autonomous disk IO under OS");
        $display("PASS MMU RT11: %0d checks, %0d clocks, %0d DMA words, %0d mapped fetches",checks,clocks,dma_words,mmu_fetches);$finish;
    end
endmodule
