`timescale 1ns/1ps
// Full CPU, FRAM, SD and UART waveforms; no CPU state or peripheral-bus forcing.
module tb_odt_rt11;
    reg clk=0,reset=1,rx=1,halt_button=0;
    wire tx,fc,fs,fm,fi,sc,ss,sm,si,boot_complete,stopped;
    wire din,ce,sck,rs,blank,latch;
    reg [7:0] shifted=0,outputs=0;
    reg [3:0] keys=0;
    integer keycode=-1,display_count=0,display_frames=0;
    reg [639:0] display_bits=0,last_display=0;
    reg [7:0] font[0:319];
    reg [7:0] corrupt_saved;
    integer clocks=0,prompts=0,loader_prompts=0,odt_prompts=0,phase=0;
    integer bus_chars=0,serial_chars=0,uart_file,b,checks=0,oldprompt=0,oldodt=0;
    integer guest_fetches=0,oldfetches=0,rx_overruns=0,guest_ops=0,oldops=0;
    integer oldframe=0,auto_clock=0,auto_interval=0,auto_pause=0,auto_step=0;
    reg [15:0] oldsp;
    reg [7:0] expected_tx[0:65535];
    reg [7:0] serial_value,previous_char=0;
    reg [63:0] window=0;
    string uart_path,segment="",cmdtext;
    `include "odt_symbols.vh"
    always #5 clk=~clk;
    // Same matrix electrical ordering as PNLDRV, not direct CPU key injection.
    always @*begin
        keys=0;
        case(keycode)
        0:if(outputs[3])keys=1; 1:if(outputs[3])keys=2;4:if(outputs[3])keys=4;7:if(outputs[3])keys=8;
        10:if(outputs[4])keys=1;2:if(outputs[4])keys=2;5:if(outputs[4])keys=4;8:if(outputs[4])keys=8;
        11:if(outputs[5])keys=1;3:if(outputs[5])keys=2;6:if(outputs[5])keys=4;9:if(outputs[5])keys=8;
        15:if(outputs[6])keys=1;14:if(outputs[6])keys=2;13:if(outputs[6])keys=4;12:if(outputs[6])keys=8;
        16:if(outputs[7])keys=1;17:if(outputs[7])keys=2;18:if(outputs[7])keys=4;19:if(outputs[7])keys=8;
        default:keys=0;
        endcase
    end
    uj11_board dut(.clk(clk),.reset(reset),.halt_button(halt_button),.uart_rx(rx),.uart_tx(tx),.panel_keys(keys),
        .panel_din(din),.panel_ce(ce),.panel_clk(sck),.panel_rs(rs),.panel_blank(blank),.panel_latch(latch),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(sc),.sd_sck(ss),.sd_mosi(sm),.sd_miso(si),.boot_complete(boot_complete),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
    spi_sd_model card(.cs_n(sc),.sck(ss),.mosi(sm),.miso(si),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    always @(posedge sck)begin
        shifted={shifted[6:0],din};
        if(!ce)begin display_bits={display_bits[638:0],din};display_count=display_count+1;end
    end
    always @(posedge latch)outputs=shifted;
    always @(negedge ce)begin display_count=0;display_bits=0;end
    always @(posedge ce)if(display_count!=0 && !rs)begin
        if(display_count!=640)$fatal(1,"bad HDSP frame: %0d bits",display_count);
        display_frames=display_frames+1;last_display=display_bits;
    end
    function [15:0] upper(input integer a);return {fram.memory[65536+a+1],fram.memory[65536+a]};endfunction
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"phase%0d %s",phase,why);checks=checks+1;end
    endtask
    task contains(input string expected);
        integer j;reg found;
        begin
            found=0;
            for(j=0;j+expected.len()<=segment.len();j=j+1)if(segment.substr(j,j+expected.len()-1)==expected)found=1;
            if(!found)$fatal(1,"phase%0d missing [%s] in [%s]",phase,expected,segment);checks=checks+1;
        end
    endtask
    initial begin
        if(!$value$plusargs("UART_LOG=%s",uart_path))$fatal(1,"UART_LOG required");
        uart_file=$fopen(uart_path,"w");wait(!reset);
        forever begin
            @(negedge tx);repeat(128)@(negedge clk);
            if(tx!==0)$fatal(1,"UART false start");
            for(b=0;b<8;b=b+1)begin repeat(257)@(negedge clk);serial_value[b]=tx;end
            repeat(257)@(negedge clk);
            if(tx!==1 || serial_chars>=bus_chars || serial_value!==expected_tx[serial_chars])$fatal(1,"UART waveform mismatch byte%0d",serial_chars);
            serial_chars=serial_chars+1;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    task check_display(input string text16);
        reg [639:0] golden;
        integer i,j,k,c;
        begin
            golden=0;check(text16.len()==16,"display expectation length");
            for(i=0;i<16;i=i+1)begin
                k=(i+8)%16;c=int'(text16[k]);
                for(j=0;j<5;j=j+1)golden={golden[631:0],font[(c-32)*5+j]};
                check(fram.memory[65536+O_SCREEN+i]==text16[i],"HDSP window text");
            end
            check(last_display==golden,"physical 640-bit glyph frame and module order");
        end
    endtask
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
    task shell(input string value);
        begin oldprompt=prompts;segment="";send_line(value);wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);end
    endtask
    task odt_command(input string value);
        begin oldodt=odt_prompts;segment="";send_line(value);wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);end
    endtask
    task keypress(input integer key);
        begin
            $display("KEY start %0d clocks%0d last%o",key,clocks,upper(O_KLAST));$fflush();
            while(upper(O_KLAST)!=65535)@(negedge clk);@(negedge clk);keycode=key;
            while(upper(O_KLAST)!=16'(key))@(negedge clk);repeat(10000)@(negedge clk);keycode=-1;
            while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
            $display("KEY done %0d clocks%0d last%o",key,clocks,upper(O_KLAST));$fflush();
        end
    endtask
    always @(posedge clk)if(!reset)begin
        clocks<=clocks+1;
        if(dut.acknowledge && dut.opcode_fetch && !dut.bank)guest_fetches<=guest_fetches+1;
        if(phase>=4 && dut.bus.fixed_uart.console.rx_busy && dut.bus.fixed_uart.console.rx_bits==9 && dut.bus.fixed_uart.console.rx_timer==0 && dut.bus.fixed_uart.console.rx_full)rx_overruns<=rx_overruns+1;
        if(dut.acknowledge && dut.writing && dut.bus.uart_selected && dut.address==16'o177566)begin
            expected_tx[bus_chars]=dut.data[7:0];bus_chars=bus_chars+1;segment={segment,dut.data[7:0]};
            window={window[55:0],dut.data[7:0]};
            if(previous_char==10 && dut.data[7:0]==".")prompts<=prompts+1;
            if(window=="UJLOAD> ")loader_prompts<=loader_prompts+1;
            if(window[39:0]=="ODT> ")odt_prompts<=odt_prompts+1;
            previous_char<=dut.data[7:0];
        end
        if(stopped)$fatal(1,"stopped phase%0d PC%o IR%o",phase,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir);
        if(clocks!=0 && clocks%20000000==0)begin $display("CP65 progress %0d phase%0d PC%o UART%0d",clocks,phase,dut.cpu.engine.dp.rf.words[7],bus_chars);$fflush();end
        if(clocks>700000000)$fatal(1,"timeout phase%0d PC%o",phase,dut.cpu.engine.dp.rf.words[7]);
    end
    initial begin
        #1;repeat(15)@(negedge clk);reset=0;wait(prompts==3);repeat(2000000)@(negedge clk);
        phase=1;shell("SET SL OFF");
        shell("RUN UJON");contains("?UJON-E-");check(!dut.cpu.engine.debug_enabled,"no ODT must reject activation");
        phase=2;oldprompt=prompts;send_line("RUN UJLOAD");wait(loader_prompts==1);repeat(10000)@(negedge clk);send_line("ODT");
        wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        check(dut.cpu.engine.service_ready[0] && !dut.cpu.engine.debug_enabled,"legacy loader installs but does not enable button");
        phase=3;corrupt_saved=fram.memory[65536+'o10014];fram.memory[65536+'o10014]=corrupt_saved^8'h01;
        shell("RUN UJON");contains("?UJON-E-");check(!dut.cpu.engine.debug_enabled,"corrupt code must reject activation");
        fram.memory[65536+'o10014]=corrupt_saved;
        shell("RUN UJON");contains("CP64 DEBUG ENABLED");check(dut.cpu.engine.debug_enabled,"activation");
        phase=4;oldodt=odt_prompts;send_line("RUN UJTEST");
        wait(dut.bus.rk_service_active);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
        wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);
        check(!dut.bus.rk_service_active,"request during RK firmware accepted only after service exit");
        send_line("C");oldodt=odt_prompts;
        wait(dut.acknowledge && !dut.bank && dut.opcode_fetch && dut.address==16'(G_LOOP));
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
        wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        contains("R4=125061");contains("R6=177777");check(upper(O_PNEN)==1,"panel owns idle HG pins");
        phase=5;oldfetches=guest_fetches;odt_command("S");check(guest_fetches==oldfetches+1,"exactly one guest instruction");
        odt_command("R 2 123456");contains("R2=123456");
        oldodt=odt_prompts;send_byte("R");send_byte(32);send_byte("2");send_byte(32);send_byte("7");
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
        wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);
        odt_command("R 2");contains("R2=123456");
        odt_command("M 177562");contains("?SYNTAX");
        phase=6;segment="";keypress(16);keypress(19); // menu -> registers
        contains("[PANEL] R");
        segment="";keypress(18);contains("R1=");
        segment="";keypress(19);
        odt_command("R 0 1");contains("?PANEL EDIT ACTIVE");check(upper(O_REGS)==16'o1234,"UART cannot change context during panel edit");
        segment="";keypress(7);keypress(19); // edit R1=7
        contains("[PANEL] R 000001 000007");check(upper(O_REGS+2)==7,"panel edit uses common register command");check_display("R1=000007       ");
        phase=7;odt_command("A 0");cmdtext=$sformatf("D %o",G_LOOP);odt_command(cmdtext);
        keypress(18);contains("[PANEL] N");check(upper(O_DCUR)==16'(G_LOOP+2),"NEXT disassembly boundary");
        keypress(17);contains("[PANEL] L");check(upper(O_DCUR)==16'(G_LOOP),"PREV known boundary");
        odt_command("A 1");oldprompt=bus_chars;oldops=guest_ops;
        cmdtext=$sformatf("%06o: 005201  INC R1",G_LOOP);
        for(integer offset=1;offset<=cmdtext.len()-16;offset=offset+1)begin
            oldframe=display_frames;auto_clock=clocks;
            while(display_frames==oldframe)@(negedge clk);
            @(negedge clk);auto_interval=clocks-auto_clock;
            if(offset==1)auto_pause=auto_interval;
            if(offset==2)auto_step=auto_interval;
            check(upper(O_SCROLL)==16'(offset),"automatic right offset");
            check_display(cmdtext.substr(offset,offset+15));
        end
        for(integer offset=cmdtext.len()-17;offset>=0;offset=offset-1)begin
            oldframe=display_frames;while(display_frames==oldframe)@(negedge clk);
            @(negedge clk);check(upper(O_SCROLL)==16'(offset),"automatic left offset");
            check_display(cmdtext.substr(offset,offset+15));
        end
        check(bus_chars==oldprompt,"automatic scrolling has no UART output");
        check(guest_ops==oldops,"automatic scrolling has no USER bus access");
        check(auto_pause>2*auto_step,"endpoint reading pause");
        $display("CP65 AUTO timing at nominal 29.56 MHz: initial %0d clocks, interior %0d clocks",auto_pause,auto_step);
        odt_command("A 0");
        // Native RT-11 image includes a nested JSR fixture outside its main loop.
        oldsp=upper(O_REGS+12);
        cmdtext=$sformatf("R 6 %o",{fram.memory[G_OLDSP+1],fram.memory[G_OLDSP]});odt_command(cmdtext);
        cmdtext=$sformatf("R 7 %o",G_OVCALL);odt_command(cmdtext);
        odt_command("T");contains("OVER RETURN");
        check(upper(O_REGS+14)==16'(G_OVDONE),"native nested STEP OVER return PC");
        check(upper(O_REGS+12)=={fram.memory[G_OLDSP+1],fram.memory[G_OLDSP]},"native STEP OVER stack depth");
        cmdtext=$sformatf("R 7 %o",G_LOOP);odt_command(cmdtext);
        cmdtext=$sformatf("R 6 %o",oldsp);odt_command(cmdtext);
        phase=8;cmdtext=$sformatf("W %o 1",G_DONE);odt_command(cmdtext);
        oldprompt=prompts;send_line("C");wait(prompts>oldprompt);wait(serial_chars==bus_chars);contains("CONTEXT RESUMED");
        check(rx_overruns==0,"UART RX at declared pacing has no overrun");
        phase=9;shell("RUN UJON");contains("CP64 DEBUG ENABLED"); // mutable context does not invalidate immutable code check
        phase=10;oldprompt=prompts;@(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
        wait(prompts>=oldprompt+3);wait(serial_chars==bus_chars);check(!dut.cpu.engine.debug_enabled,"cold reset disables ODT");
        $display("PASS CP65 RT11 + panel: %0d checks, %0d clocks, %0d UART bytes, %0d HDSP frames",checks,clocks,serial_chars,display_frames);
        $fclose(uart_file);$finish;
    end
endmodule
