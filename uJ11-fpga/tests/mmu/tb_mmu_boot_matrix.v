`timescale 1ns/1ps
// Full SD bootstrap -> RT-11 using actual CPU, SERV, SRAM and UART waveforms.
module tb_mmu_boot_matrix #(parameter TICK_DIVISOR=480000);
    reg clk=0,power_on=1,rx=1,halt_button=0;
    always #20.833 clk=~clk;
    wire initialized,tx,boot_complete,stopped;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire cs,sck,mosi,miso;wire [7:0] pins;
    string boot_case;
    integer max_clocks=2000000000;
    initial if($value$plusargs("MAX_CLOCKS=%d",max_clocks))begin end
    initial if(!$value$plusargs("CASE=%s",boot_case))$fatal(1,"CASE required");
    integer clocks=0,bus_chars=0,serial_chars=0,prompts=0,phase=0,checks=0;
    integer dma_words=0,concurrent_fetches=0,mmu_fetches=0,uart_file,b;
    reg [7:0] expected[0:65535],serial_value,previous_char=0;
    string segment="",uart_path,monitor;
    uj11_mmu_board #(.TICK_DIVISOR(TICK_DIVISOR)) dut(.clk(clk),.reset(power_on || !initialized),.power_on(power_on),
        .uart_rx(rx),.halt_button(halt_button),.memory_initialized(initialized),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete),.stopped(stopped));
    serv_memory_guard guard(.clk(clk),.reset(dut.bus.disk.iop_reset),
        .write(dut.bus.disk.data_accept && dut.bus.disk.memory_selected && dut.bus.disk.de),.address(dut.bus.disk.da));
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
    task wait_text(input string wanted);
        bit found;integer last_chars;
        begin
            found=0;last_chars=-1;
            while(!found)begin
                if(last_chars!=bus_chars)begin
                    for(integer i=0;i+wanted.len()<=segment.len();i++)
                        if(segment.substr(i,i+wanted.len()-1)==wanted)found=1;
                    last_chars=bus_chars;
                end
                @(negedge clk);
            end
            wait(serial_chars==bus_chars);
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
        forever begin : receive_frame
            @(negedge tx);
            for(integer sample=0;sample<10;sample++)begin
                repeat(sample==0?104:208)begin
                    @(negedge clk);
                    // PDP RESET resets the UART, including its shifter and
                    // holding register. Abort this frame and resync on start.
                    if(dut.peripheral_reset)disable receive_frame;
                end
                if(sample==0)check(tx===0,"UART start bit");
                else if(sample<9)serial_value[sample-1]=tx;
            end
            check(tx===1 && serial_chars<bus_chars && serial_value===expected[serial_chars],$sformatf("UART waveform got=%h expected=%h counts=%d/%d stop=%b PC=%o",serial_value,expected[serial_chars],serial_chars,bus_chars,tx,dut.cpu.pc));
            serial_chars++;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    always @(posedge clk) if($test$plusargs("TRACE_IO") && dut.writing && dut.bus.disk.data_accept && dut.bus.disk.csr_selected && dut.bus.disk.de && dut.bus.disk.da[4:2]==2)
        $display("SERV IO %o W=%b DATA=%o R=%h PC=%o at %t",dut.bus.offset,dut.writing,dut.write_data,dut.bus.disk.dw,dut.cpu.pc,$time);
    always @(posedge clk) begin
        clocks<=clocks+1;
        if(dut.peripheral_reset)serial_chars=bus_chars;
        if(initialized && !power_on)begin
            if(dut.bus.dma_request && dut.bus.dma_ready)dma_words++;
            if(dut.cpu.step && dut.cpu.fetching)begin
                if(dut.bus.disk.pending)concurrent_fetches++;
                if(dut.cpu.mmr0[0])mmu_fetches++;
            end
            if(dut.request && dut.ready && dut.writing && dut.address==22'o17777566)begin
                expected[bus_chars]=dut.write_data[7:0];bus_chars++;
                segment={segment,{1'b0,dut.write_data[6:0]}};
                if(boot_case=="rsx-rq1" && segment.len()>=23 &&
                    segment.substr(segment.len()-23,segment.len()-1)=="Reserved inst execution")
                    $fatal(1,"RSX task aborted on a reserved instruction");
                if((previous_char==10 || previous_char==13) &&
                    dut.write_data[6:0]==(boot_case=="rsx-rq1" ? ">" : "."))prompts++;
                previous_char={1'b0,dut.write_data[6:0]};
            end
            if(stopped && boot_case!="rsx-rq0")$fatal(1,"unexpected console PC=%o IR=%o MMR0=%o MMR2=%o",dut.cpu.pc,dut.cpu.ir,dut.cpu.mmr0,dut.cpu.mmr2);
        end
        if(clocks>max_clocks)$fatal(1,"timeout phase%0d PC=%o IR=%o uPC=%h",phase,dut.cpu.pc,dut.cpu.ir,dut.cpu.upc);
        if(clocks!=0 && clocks%20000000==0)begin
            $display("MMU boot progress %0d phase%0d PC=%o MMR0=%o DMA=%0d UART=%0d",clocks,phase,dut.cpu.pc,dut.cpu.mmr0,dma_words,bus_chars);$fflush();
            if($test$plusargs("TRACE_IO"))begin
                $display("CPU access %o %b %b =%o SD reads=%d engine=%d",dut.address,dut.request,dut.ready,dut.read_data,card.read_count,dut.bus.disk.engine.mode);
                for(integer a='o3500;a<'o3670;a+=2)$display("PDP %o %o",a,{ram.memory[a+1],ram.memory[a]});
            end
        end
    end
    reg [15:0] trace_pc[0:127],trace_ir[0:127],trace_ps[0:127];
    integer trace_index=0;
    always @(posedge clk)begin
        if(dut.cpu.step && dut.cpu.fetching)begin
            trace_pc[trace_index]=dut.cpu.pc;trace_ir[trace_index]=dut.cpu.incoming;trace_ps[trace_index]=dut.cpu.psw;
            trace_index=(trace_index+1)%128;
        end
        if(dut.cpu.step && dut.cpu.upc==12'h042)begin
            $display("ILLEGAL pc=%o ir=%o PSW=%o",dut.cpu.instruction_pc,dut.cpu.ir,dut.cpu.psw);
            if(boot_case=="rsx-rq1")begin
                for(integer i=0;i<128;i++)$display("TRACE %o %o PS=%o",trace_pc[(trace_index+i)%128],trace_ir[(trace_index+i)%128],trace_ps[(trace_index+i)%128]);
            end
        end
    end
    initial begin
        if(!$value$plusargs("MONITOR=%s",monitor))monitor="fb";
        repeat(5)@(negedge clk);power_on=0;
        if(boot_case=="rsx-rq0")begin
            wait(stopped);wait(serial_chars==bus_chars);
            contains("THIS VOLUME DOES NOT CONTAIN A HARDWARE BOOTABLE SYSTEM");
            check(boot_complete && card.writes==0,"original RQ0 placeholder halts without writes");
            check(dut.cpu.pc==16'o34,"original RQ0 HALT PC matches SIMH");
            $display("PASS MMU matrix RQ0: original nonbootable volume reaches HALT, PC=%o",dut.cpu.pc);$finish;
        end
        if(boot_case=="rsx-rq1")begin
            wait_text("[S]: ");contains("RSX-11M-PLUS V4.6  BL87");
            // The prompt write completes before AT queues its terminal read.
            // Give that QIO time to attach before sending the user's reply.
            repeat(12000000)@(negedge clk);
            shell("12:00 28-SEP-1999");
            wait_text("QUE BAP0:/BATCH");settled_prompt();
            phase=1;shell("DEV DU:");contains("DU0:");contains("DU1:");
            phase=2;shell("PIP DU1:[1,54]RSX11M.SYS/LI");contains("RSX11M.SYS;1");contains("1026.");
        end else begin
            settled_prompt();contains(boot_case=="rt11xm" ? "RT-11XM" : "RT-11SJ");
            if(boot_case=="rt11xm")begin
                contains("RH11/HK");
                contains("RK05, units: 0 1 2");
                shell("SET SL OFF");
                phase=1;shell("SHOW CONFIGURATION");contains("Booted from DM0:RT11XM");
                contains("2048KB of memory");contains("22 bit addressing is on");
                phase=2;shell("SHOW MEMORY");contains("10000000  MEMTOP");
                phase=3;shell("DIR DM1:");contains("ADDER");
                phase=4;shell("DIR RK0:BASIC.SAV");contains("BASIC");
                phase=5;shell("DIR RK1:XM.SAV");contains("XM");
                phase=6;shell("DIR RK2:FORTRA.SAV");contains("FORTRA");
                check(mmu_fetches>1000,"XM memory management active");
            end else begin
                phase=1;shell("DIR RK0:RT11*.SYS");contains("RT11SJ");
                phase=2;shell("TYPE V4USER.TXT");contains("Welcome to RT-11 Version 4");
            end
        end
        check(boot_complete && dma_words>1000,"bootstrap and autonomous disk DMA");
        $display("PASS MMU matrix %s: %0d checks, %0d clocks, %0d DMA words",boot_case,checks,clocks,dma_words);$finish;
    end
endmodule
