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
    integer guest_fetches=0,oldfetches=0,rx_overruns=0;
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
            if(window[55:0]=="UJMOD> ")loader_prompts<=loader_prompts+1;
            if(window[39:0]=="ODT> ")odt_prompts<=odt_prompts+1;
            previous_char<=dut.data[7:0];
        end
        if(stopped)$fatal(1,"stopped phase%0d PC%o IR%o",phase,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir);
        if(clocks!=0 && clocks%20000000==0)begin $display("CP76 progress %0d phase%0d PC%o UART%0d",clocks,phase,dut.cpu.engine.dp.rf.words[7],bus_chars);$fflush();end
        if(clocks>2000000000)$fatal(1,"timeout phase%0d PC%o",phase,dut.cpu.engine.dp.rf.words[7]);
    end
    task module_command(input string command);
        integer oldloader;
        begin
            oldprompt=prompts;oldloader=loader_prompts;segment="";
            send_line("RUN UJMOD");wait(loader_prompts>oldloader);
            wait(serial_chars==bus_chars);send_line(command);
            wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        end
    endtask
    task cold;
        integer count;
        begin
            wait(serial_chars==bus_chars);count=prompts;
            @(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
            wait(prompts>=count+3);wait(serial_chars==bus_chars);repeat(2000000)@(negedge clk);
            shell("SET SL OFF");
        end
    endtask
    initial begin
        #1;repeat(15)@(negedge clk);reset=0;wait(prompts==3);repeat(2000000)@(negedge clk);
        phase=1;shell("SET SL OFF");
        module_command("ODT");contains("!UJMOD-I-DONE");
        module_command("SDBOOT");contains("!UJMOD-I-DONE");
        module_command("FP11");contains("!UJMOD-I-DONE");
        check(upper('o7020)=='o40000 && upper('o7026)=='hc103,"native loader installed FP in third slot");
        check(!dut.cpu.engine.service_ready[1],"FP inactive until cold init");
        phase=2;cold();
        check(dut.cpu.engine.service_ready==3 && dut.cpu.engine.debug_enabled,"all services initialized");
        check(upper('o7006)=='hc107 && upper('o7016)=='hc107 && upper('o7026)=='hc107,"all three module checksums accepted");
        check(upper(F_FPS)==0 && upper(F_ACS+46)==0,"initial FP state zero");
        // The real UART command enters ODT before the first FP opcode retires.
        phase=3;oldodt=odt_prompts;send_line("RUN FPTST");
        wait(dut.acknowledge && !dut.bank && dut.opcode_fetch && dut.address==G_ENTRY);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
        wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        // Stop may arrive after SETD dispatch. Set an exact starting context
        // through normal debugger commands, with no CPU/FRAM state forcing.
        odt_command($sformatf("R 7 %o",G_ENTRY));
        odt_command("S");check(upper(F_FPS)==16'o200,"SETD");
        check(upper(O_REGS+14)==G_LOAD,"one SETD");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"LDD source");
        odt_command("S");check(upper(F_ACS+16)==16'o140200 && upper(F_FPS)==16'o210,"NEGD flips sign/N");
        odt_command("S");check(upper(F_ACS+16)==16'o40200 && upper(F_FPS)==16'o200,"ABSD clears sign/N");
        odt_command("S");check(upper(F_FPS)==16'o204,"CMPD exact equality");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"CMP preserves nonzero AC");
        odt_command("S");check(upper(F_FPS)==16'o200,"TSTD flags");
        odt_command("S");check(upper(F_FPS)==16'o204,"CLRD flags");
        for(integer i=0;i<4;i++)check(upper(F_ACS+40+2*i)==0,"CLRD clears AC5");
        odt_command("S");check(upper(F_ACS+16)==16'o40400 && upper(F_FPS)==16'o200,"ADDD doubles magnitude");
        for(integer i=1;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"ADDD exact fraction");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"SUBD restores full D operand");
        odt_command("S");check(upper(F_ACS+16)==16'o40400,"MULD by two");
        for(integer i=1;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"MULD full D fraction");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"DIVD restores source");
        odt_command("S");check(upper(F_FPS)==0,"SETF before arithmetic");
        odt_command("S");check(upper(F_ACS)==0 && upper(F_FPS)==4,"CLRF seeds arithmetic accumulator");
        odt_command("S");check(upper(F_ACS)==16'o40200 && upper(F_FPS)==0,"ADDF immediate one");
        odt_command("S");check(upper(F_ACS)==16'o40500 && upper(F_FPS)==0,"MULF one by three");
        odt_command("S");check(upper(F_ACS)==16'o40200 && upper(F_FPS)==0,"DIVF three by three");
        odt_command("S");check(upper(F_ACS)==0 && upper(F_FPS)==4,"SUBF exact cancellation");
        odt_command("S");check(upper(F_FPS)==16'o204,"SETD after arithmetic");
        odt_command("S");check(upper(F_FPS)==16'o44000,"enable FIUV with FID");
        odt_command("S");check(upper(F_FPS)==16'o144014,"FP11-A TST completes FN/FZ before UV exception");
        check(upper(F_FEC)==16'o14 && upper(F_FEA)==G_BADTST,"undefined variable FEC/FEA");
        odt_command("S");check({fram.memory[G_RESULT+1],fram.memory[G_RESULT]}==16'o144014,"STFPS result");
        odt_command("S");
        check({fram.memory[G_ERR+1],fram.memory[G_ERR]}==16'o14 && {fram.memory[G_ERR+3],fram.memory[G_ERR+2]}==G_BADTST,"STST records TST address");
        odt_command("S");check((upper(F_FPS)&16'o200)==0,"SETF for MOD");
        odt_command("S");check(upper(F_ACS)==16'o40300,"LDF 1.5 for MOD");
        odt_command("S");check(upper(F_ACS)==16'o37600 && upper(F_ACS+8)==16'o40400,"MODF fraction 0.25 / integer 2");
        odt_command("S");check((upper(F_FPS)&16'o200)!=0,"SETD for MOD");
        odt_command("S");check(upper(F_ACS)==16'o40300,"LDD 1.5 for MOD");
        odt_command("S");check(upper(F_ACS)==16'o37600 && upper(F_ACS+8)==16'o40400,"MODD fraction 0.25 / integer 2");
        for(integer i=1;i<4;i++)check(upper(F_ACS+2*i)==0 && upper(F_ACS+8+2*i)==0,"MODD both complete low words");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+8+2*i)==0,"MODD odd AC discards integer");
        check(upper(F_ACS)==16'o37600,"MODD odd AC preserves preceding AC");
        odt_command("S");check(upper(F_ACS)==16'hc780,"LDCID minimum signed word");
        odt_command("S");check(upper(O_REGS)==16,"STEXP unbiased exponent");
        odt_command("S");check(upper(F_ACS)==16'hc080,"LDEXP produces minus one");
        odt_command("S");check(upper(O_REGS+2)==16'hffff && (upper(O_REGS+16)&15)==8,"STCDI updates CPU flags");
        odt_command("S");check(upper(F_ACS+8)==16'hc080,"STCDF result in AC1");
        odt_command("S");check((upper(F_FPS)&16'o200)==0,"SETF conversions");
        odt_command("S");check(upper(F_ACS)==16'hc080,"LDCDF result in AC0");
        odt_command("S");check((upper(F_FPS)&16'o100)!=0,"SETL conversions");
        odt_command("S");check(upper(F_ACS)==16'h4880,"LDCLF immediate is left justified");
        odt_command("S");check(upper(O_REGS+2)==1 && (upper(O_REGS+16)&15)==0,"STCFL stores high word and flags");
        odt_command("S");check(upper(F_ACS+8)==16'h4880 && upper(F_ACS+12)==0 && upper(F_ACS+14)==0,"STCFD zero extends");
        odt_command("S");check((upper(F_FPS)&16'o200)!=0,"SETD conversions");
        odt_command("S");check(upper(F_ACS)==16'h4880 && upper(F_ACS+4)==0 && upper(F_ACS+6)==0,"LDCFD zero extends");
        odt_command("S");check((fram.memory[G_LONG]|(fram.memory[G_LONG+1]<<8))==1 && (fram.memory[G_LONG+2]|(fram.memory[G_LONG+3]<<8))==0,"STCDL writes two words");
        check(upper(O_REGS+14)==G_LOOP,"ODT stopped after forty-three FP instructions");
        check(upper('o10)==F_ENTER && upper('o110)==O_ENTER,"FP/debug vectors coexist");
        odt_command("D");contains("BR");
        odt_command($sformatf("R 7 %o",G_FINISH));
        oldprompt=prompts;send_line("C");wait(prompts>oldprompt);wait(serial_chars==bus_chars);
        contains("RETURNED TO RT11");shell("DIR FPTST.SAV");contains("FPTST");
        phase=4;cold();
        check(upper(F_FPS)==0,"FP mutable state resets without reload");
        check(dut.cpu.engine.service_ready==3 && dut.cpu.engine.debug_enabled,"ODT and FP retained together");
        module_command("OFF2");contains("!UJMOD-I-DONE");
        phase=5;cold();check(dut.cpu.engine.service_ready==1,"FP can be disabled without removing ODT");
        check(upper('o7026)=='hc101,"disabled FP remains allocated");
        check(rx_overruns==0,"UART has no receive overrun");
        $display("PASS CP76 RT11 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end

endmodule
