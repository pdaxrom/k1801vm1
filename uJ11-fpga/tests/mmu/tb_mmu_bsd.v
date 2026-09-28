`timescale 1ns/1ps
// Full 2.9BSD multiuser boot using actual CPU, SERV, SRAM and UART waveforms.
module tb_mmu_bsd;
    reg clk=0,power_on=1,rx=1,halt_button=0;
    always #20.833 clk=~clk;
    wire initialized,tx,boot_complete,stopped;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire cs,sck,mosi,miso;wire [7:0] pins;
    longint max_clocks=64'd6000000000;
    longint clocks=0;integer bus_chars=0,serial_chars=0,prompts=0,phase=0,checks=0;
    integer dma_words=0,concurrent_fetches=0,mmu_fetches=0,uart_file,b;
    reg [7:0] expected[0:65535],serial_value,previous_char=0;
    string segment="",uart_path,monitor;
    uj11_mmu_board dut(.clk(clk),.reset(power_on || !initialized),.power_on(power_on),
        .uart_rx(rx),.halt_button(halt_button),.memory_initialized(initialized),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete),.stopped(stopped));
    serv_memory_guard guard(.clk(clk),.reset(dut.bus.disk.iop_reset),
        .write(dut.bus.disk.data_accept && dut.bus.disk.memory_selected && dut.bus.disk.de),.address(dut.bus.disk.da));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model #(.SECTORS(2048)) card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(1'b0),
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
            if(phase>=11)$display("BSD input %h at %0d",value,clocks);
            @(negedge clk);rx=0;repeat(208)@(negedge clk);
            for(integer bitno=0;bitno<8;bitno++)begin rx=value[bitno];repeat(208)@(negedge clk);end
            // A DL11 has one receive holding register. Pace terminal input
            // for the BSD interrupt handler rather than blasting a paste.
            rx=1;repeat(480000)@(negedge clk);
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
            serial_chars++;$fwrite(uart_file,"%c",serial_value & 127);$fflush(uart_file);
        end
    end
    integer trace_index=0,irq_count=0;
    reg [47:0] trace_ring[0:127];
    always @(posedge clk) begin
        if(dut.cpu.step && dut.cpu.fetching)begin
            trace_ring[trace_index%128]={dut.cpu.instruction_pc,dut.cpu.incoming,dut.cpu.psw};trace_index++;
        end
        if(dut.cpu.step && dut.cpu.upc==12'h042)begin
            $display("ILLEGAL pc=%o ir=%o PSW=%o",dut.cpu.instruction_pc,dut.cpu.ir,dut.cpu.psw);
            if(dut.cpu.psw[15:14]==0 && dut.cpu.ir[15:12]!=15)
                for(integer j=0;j<128;j++)$display("TRACE %012h",trace_ring[(trace_index+j)%128]);
        end
        if(dut.irq_ack && dut.irq_vector!=16'o100 && dut.irq_vector!=16'o64)begin
            if(irq_count<300)$display("IRQ pc=%o vector=%o PSW=%o",dut.cpu.pc,dut.irq_vector,dut.cpu.psw);
            irq_count++;
        end
        if(phase>=11 && dut.bus.console.rx_csr_write)
            $display("BSD RCSR write %o pc=%o",dut.write_data,dut.cpu.pc);
        if(phase>=11 && dut.bus.console.rx_buffer_read)
            $display("BSD RBUF read %h pc=%o",dut.bus.console.rx_buffer,dut.cpu.pc);

        clocks<=clocks+1;
        if(initialized && !power_on)begin
            if(dut.bus.console.rx_overflow)$fatal(1,"BSD terminal RX overrun");
            if(dut.cpu.fault!=0)
                $display("FAULT %d pc=%o ir=%o va=%o psw=%o MMR0=%o MMR1=%o MMR2=%o frame=%b",dut.cpu.fault,dut.cpu.instruction_pc,dut.cpu.ir,dut.cpu.read_a,dut.cpu.psw,dut.cpu.mmr0,dut.cpu.mmr1,dut.cpu.mmr2,dut.cpu.trap_frame);
            if(dut.request && dut.ready && dut.error)
                $display("BUS ERROR PC=%o instruction=%o addr=%o write=%b data=%o PSW=%o MMR0=%o MMR3=%o",dut.cpu.pc,dut.cpu.instruction_pc,dut.address,dut.writing,dut.write_data,dut.cpu.psw,dut.cpu.mmr0,dut.cpu.mmr3);
            if(dut.bus.dma_request && dut.bus.dma_ready)dma_words++;
            if(dut.cpu.step && dut.cpu.fetching)begin
                if(dut.bus.disk.pending)concurrent_fetches++;
                if(dut.cpu.mmr0[0])mmu_fetches++;
            end
            if(dut.request && dut.ready && dut.writing && dut.address==22'o17777566)begin
                expected[bus_chars]=dut.write_data[7:0];bus_chars++;
                segment={segment,1'b0,dut.write_data[6:0]};
                if(previous_char=="#" && dut.write_data[6:0]==" ")prompts++;
                previous_char={1'b0,dut.write_data[6:0]};
            end
            if(stopped)$fatal(1,"unexpected console PC=%o IR=%o MMR0=%o MMR2=%o",dut.cpu.pc,dut.cpu.ir,dut.cpu.mmr0,dut.cpu.mmr2);
        end
        if(clocks>max_clocks)$fatal(1,"timeout phase%0d PC=%o IR=%o uPC=%h",phase,dut.cpu.pc,dut.cpu.ir,dut.cpu.upc);
        if(clocks!=0 && clocks%20000000==0)begin
            $display("MMU boot progress %0d phase%0d PC=%o MMR0=%o DMA=%0d UART=%0d",clocks,phase,dut.cpu.pc,dut.cpu.mmr0,dma_words,bus_chars);$fflush();
            if(phase==1 || phase>=11)$display("BSD UART serial=%0d login=%b IE=%b full=%b irq=%b psw=%o segmentlen=%0d",serial_chars,has("login: "),dut.bus.console.rx_ie,dut.bus.console.rx_full,dut.bus.console.rx_irq_o,dut.cpu.psw,segment.len());
        end
    end
    function bit has(input string wanted);
        begin
            has=0;
            for(integer i=0;i+wanted.len()<=segment.len();i++)
                if(segment.substr(i,i+wanted.len()-1)==wanted)has=1;
        end
    endfunction
    task await_text(input string wanted);
        begin
            // Check between characters, not every clock.
            while(!has(wanted))begin
                repeat(50000)@(negedge clk);
                check(!has("Trap in Boot") && !has("panic:"),"BSD trapped");
            end
            wait(serial_chars==bus_chars);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;
        await_text("other key=menu: ");contains("Default: RL0");contains("units: 0 1");
        send_byte(13);segment="";
        await_text("70Boot");repeat(500000)@(negedge clk);
        begin static string command="rl(0,0)rlunix";for(integer i=0;i<command.len();i++)send_byte(command[i]);end
        send_byte(13);
        await_text("# ");settled_prompt();contains("Berkeley UNIX");contains("rl 0 csr 174400");contains("xp 0 csr 176700");
        check(boot_complete,"bootstrap released");phase=1;
        segment="";send_byte(4);await_text("login: ");contains("Mounted /usr");
        phase=11;$display("BSD login prompt at %0d",clocks);
        // This getty prints its prompt, sleeps, then flushes pending input.
        // Allow two real seconds for its HZ-based delay on the 50 Hz board.
        segment="";repeat(48000000)@(negedge clk);phase=12;send_byte("r");send_byte("o");send_byte("o");send_byte("t");send_byte(13);phase=13;
        await_text("# ");contains("Welcome to the 2.9BSD");
        phase=2;shell("ls /usr");contains("bin");contains("lib");
        phase=3;shell("cat /etc/fstab");contains("/dev/rl1:swap");contains("/dev/xp0h:/usr");
        phase=4;shell("echo SERV-RL-WRITE > /tmp/serv-test");
        shell("cat /tmp/serv-test");contains("SERV-RL-WRITE");
        phase=5;shell("echo SERV-RP-WRITE > /usr/tmp/serv-test");
        shell("cat /usr/tmp/serv-test");contains("SERV-RP-WRITE");
        shell("rm /tmp/serv-test /usr/tmp/serv-test");shell("sync");
        check(mmu_fetches>1000 && dma_words>1000 && concurrent_fetches>100,"BSD MMU and autonomous IO");
        $display("PASS MMU BSD: %0d checks, %0d clocks, %0d DMA words, %0d mapped fetches",checks,clocks,dma_words,mmu_fetches);$finish;
    end
endmodule
