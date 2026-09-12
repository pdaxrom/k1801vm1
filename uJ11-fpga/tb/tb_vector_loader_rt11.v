`timescale 1ns/1ps
// Cold RT-11 + CP62 RK recovery board. Installation is performed by UJLOAD.SAV.
module tb_loader_rt11;
    reg clk=0,reset=1,rx=1,fail_read=0;
    wire tx,fc,fs,fm,fi,sc,ss,sm,si,boot_complete,stopped;
    // Corrupt the selected file's first payload sector only. The RT-11 USR
    // and SWAP.SYS must remain readable for the monitor to report the error.
    wire file_read_error=fail_read &&
        {card.packet[1],card.packet[2],card.packet[3],card.packet[4]}==`CP62_ERROR_LBA;
    integer clocks=0,prompts=0,loader_prompts=0,scenario=0,injection=0;
    integer bus_chars=0,serial_chars=0,uart_file,b,case_count=0,write_windows=0;
    integer ctrl_reads=0,ctrl_overruns=0;
    integer base_prompt,base_load_prompt,upper_writes=0,retirements=0;
    reg [7:0] expected_tx[0:65535];
    reg [7:0] expected_upper[0:65535],snapshot[0:65535];
    reg [7:0] serial_value,previous_char=0;
    reg [63:0] window=0;
    reg injected=0,allow_context=0,finished=0;
    string uart_path;
    always #5 clk=~clk;
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(rx),.uart_tx(tx),.panel_keys(4'b0),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(sc),.sd_sck(ss),.sd_mosi(sm),.sd_miso(si),.boot_complete(boot_complete),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
    spi_sd_model card(.cs_n(sc),.sck(ss),.mosi(sm),.miso(si),.absent(1'b0),
        .fail_read(file_read_error),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    // Only the target upper FRAM is write-protected during fault injection.
    // Disk DMA, stack and monitor workspace in the lower bank remain writable.
    always @(negedge fs) fram.write_protect=(injection==2 && fram.phase==3 && fram.address>=24'o210000);
    initial begin
        if(!$value$plusargs("UART_LOG=%s",uart_path))uart_path="build/cp62-loader/uart.txt";
        uart_file=$fopen(uart_path,"w");if(uart_file==0)$fatal(1,"UART open");
        wait(!reset);
        forever begin
            @(negedge tx);repeat(128)@(negedge clk);
            if(tx!==0)$fatal(1,"UART false start");
            for(b=0;b<8;b=b+1)begin repeat(257)@(negedge clk);serial_value[b]=tx;end
            repeat(257)@(negedge clk);
            if(tx!==1 || serial_chars>=bus_chars || serial_value!==expected_tx[serial_chars])
                $fatal(1,"UART waveform byte %0d got %h expected %h stop%b queued%0d",serial_chars,serial_value,expected_tx[serial_chars],tx,bus_chars);
            serial_chars=serial_chars+1;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    task send_byte(input [7:0] value);
        integer bitno;
        begin
            @(negedge clk);rx=0;repeat(257)@(negedge clk);
            for(bitno=0;bitno<8;bitno=bitno+1)begin rx=value[bitno];repeat(257)@(negedge clk);end
            rx=1;repeat(50000)@(negedge clk);
        end
    endtask
    task send_line(input string value);
        integer j;
        begin for(j=0;j<value.len();j=j+1)send_byte(value[j]);send_byte(13);end
    endtask
    task run_case(input integer id,input string name,input integer want,input integer inject);
        begin
            scenario=id;injection=inject;injected=0;fail_read=0;
            if(inject==5)begin fram.memory[65536+'o166]=fram.memory[65536+'o166]^8'h01;injected=1;end
            base_prompt=prompts;base_load_prompt=loader_prompts;
            $display("\nCP62 CASE %0d %s start %0d",id,name,clocks);
            if(name=="@UJCHEK")begin allow_context=1;send_line("RUN UJCHEK");end
            else begin
                send_line("RUN UJLOAD");wait(loader_prompts>base_load_prompt);
                repeat(10000)@(negedge clk);send_line(name);
            end
            wait(prompts>base_prompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            if(dut.cpu.engine.service_ready!==2'(want))$fatal(1,"case %0d ready %b wanted %0d",id,dut.cpu.engine.service_ready,want);
            if((inject==1 || inject==3) && !injected)$fatal(1,"fault injection not reached case %0d",id);
            fail_read=0;injection=0;case_count=case_count+1;
            $display("PASS CP62 case %0d %s ready%0d at %0d",id,name,want,clocks);$fflush();
        end
    endtask
    task check_upper(input string path);
        integer j;
        begin
            $readmemh(path,expected_upper);
            for(j=0;j<65536;j=j+1)
                if(!(j>=64 && j<68) && fram.memory[65536+j]!==expected_upper[j])
                    $fatal(1,"case %0d upper %o got %h want %h",scenario,j,fram.memory[65536+j],expected_upper[j]);
        end
    endtask
    task run_interrupted;
        integer j;
        begin
            for(j=0;j<65536;j=j+1)snapshot[j]=fram.memory[65536+j];
            scenario=90;ctrl_reads=0;ctrl_overruns=0;injection=4;injected=0;base_prompt=prompts;base_load_prompt=loader_prompts;
            send_line("RUN UJLOAD");wait(loader_prompts>base_load_prompt);repeat(10000)@(negedge clk);send_line("BIGFP");
            wait(injected);send_byte(3);send_byte(3);
            wait(prompts>base_prompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            if(dut.cpu.engine.service_ready!==2'b01)$fatal(1,"CTRL/C did not leave only ODT ready");
            if(ctrl_reads!=2 || ctrl_overruns!=0)$fatal(1,"CTRL/C UART reads%0d overruns%0d",ctrl_reads,ctrl_overruns);
            for(j=0;j<16384;j=j+1)if(!(j>=64 && j<68) && fram.memory[65536+j]!==snapshot[j])$fatal(1,"CTRL/C changed other module/system %o",j);
            injection=0;$display("PASS CP62 CTRL/C during payload transfer, other module intact; UART reads%0d overruns%0d",ctrl_reads,ctrl_overruns);
        end
    endtask
    task reset_persistent;
        integer j,oldprompts;
        begin
            for(j=0;j<65536;j=j+1)snapshot[j]=fram.memory[65536+j];
            oldprompts=prompts;@(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
            wait(prompts>=oldprompts+3);wait(serial_chars==bus_chars);
            if(dut.cpu.engine.service_ready!==0 || dut.cpu.engine.service_mode!==0)$fatal(1,"cold reset reactivated FRAM");
            check_upper("build/cp62-loader/cold.hex");
            $display("PASS CP62 cold reboot: RT-11 returned, stale FRAM remains disabled");
        end
    endtask
    always @(posedge clk) if(!reset)begin
        clocks<=clocks+1;
        if(scenario==90 && dut.acknowledge && !dut.writing && dut.bus.uart_selected && dut.address==16'o177562 && dut.rdata[7:0]==3)begin
            ctrl_reads<=ctrl_reads+1;
            $display("CTRL/C read at %0d status%h",clocks,dut.rdata);
        end
        if(scenario==90 && dut.bus.fixed_uart.console.rx_busy && dut.bus.fixed_uart.console.rx_bits==9 && dut.bus.fixed_uart.console.rx_timer==0 && dut.bus.fixed_uart.console.rx_full)ctrl_overruns<=ctrl_overruns+1;
        if(dut.cpu.retire)retirements<=retirements+1;
        if(stopped)$fatal(1,"case %0d stopped PC%o IR%o uPC%h",scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir,dut.cpu.debug_upc);
        if(dut.acknowledge && dut.writing && dut.bank)upper_writes<=upper_writes+1;
        if(dut.bus_request && dut.physical &&
           (dut.bus.uart_strobe || dut.bus.sd_strobe || dut.bus.ltc_selected || dut.bus.panel_selected || dut.bus.rk_selected))
            $fatal(1,"raw FRAM access selected a peripheral at %o",dut.address);
        if(dut.acknowledge && dut.opcode_fetch && !dut.bank)begin
            if(injection==3 && !injected && dut.address==`CP62_VERIFY)begin
                fram.memory[65536+'o10000]=fram.memory[65536+'o10000]^8'h01;injected=1;
            end
            if(injection==4 && !injected && dut.address==`CP62_COPY)injected=1;
        end
        // Raw upper addresses that equal UART CSRs are RAM, not UART writes.
        if(dut.acknowledge && dut.writing && dut.bus.uart_selected && dut.address==16'o177566)begin
            if(bus_chars>=65536)$fatal(1,"UART capacity");
            expected_tx[bus_chars]=dut.data[7:0];bus_chars=bus_chars+1;
            window<={window[55:0],dut.data[7:0]};previous_char<=dut.data[7:0];
            if(previous_char==10 && dut.data[7:0]==".")prompts<=prompts+1;
            if({window[55:0],dut.data[7:0]}=="UJLOAD> ")loader_prompts<=loader_prompts+1;
            if({window[55:0],dut.data[7:0]}=="disabled")begin
                write_windows<=write_windows+1;
                if(injection==1 && !injected)begin fail_read=1;injected=1;end
            end
            if(injection==1 && {window[55:0],dut.data[7:0]}=="?UJLOAD-")fail_read=0;
        end
        if(clocks!=0 && clocks%100000000==0)begin
            $display("CP62 progress %0d clocks case%0d PC%o IR%o prompts%0d",clocks,scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir,prompts);$fflush();
        end
        if(clocks>=2100000000)$fatal(1,"CP62 timeout case%0d PC%o",scenario,dut.cpu.engine.dp.rf.words[7]);
    end
    initial begin
        #1;for(integer j=65536;j<131072;j=j+1)fram.memory[j]=8'ha5;
        repeat(15)@(negedge clk);reset=0;wait(prompts==3);repeat(10000)@(negedge clk);
        base_prompt=prompts;send_line("SET SL OFF");wait(prompts>base_prompt);repeat(10000)@(negedge clk);
        if($test$plusargs("CTRL_ONLY"))begin
            run_case(3,"ODT",1,0);
            run_interrupted();
        end else begin
            `include "loader_cases.vh"
        end
        wait(serial_chars==bus_chars);$fclose(uart_file);
        if($test$plusargs("CTRL_ONLY"))$display("PASS CP62 focused CTRL/C: %0d clocks",clocks);
        else $display("PASS CP62 full RT-11 loader: %0d file/command cases + CTRL/C + cold reboot; %0d clocks, %0d retired, %0d upper writes, %0d wire bytes",case_count,clocks,retirements,upper_writes,serial_chars);
        $finish;
    end
endmodule
