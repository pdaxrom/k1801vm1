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
        check(upper('o7020)=='o60000 && upper('o7026)=='hc103,"native loader installed FP in third slot");
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
        odt_command("F 6");contains("FPS=000200 MODE=D,I");
        odt_command($sformatf("D %o",G_LOAD));contains("LDD");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"LDD source");
        odt_command("F 2");contains($sformatf("AC2=%06o %06o %06o %06o",upper(F_ACS+16),upper(F_ACS+18),upper(F_ACS+20),upper(F_ACS+22)));
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
        odt_command("F 7");contains("FEC=000014");
        odt_command("F 10");contains($sformatf("FEA=%06o",G_BADTST));
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
        odt_command("S");check(upper(F_ACS)==16'hc800,"LDCID minimum signed word");
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
        $display("PASS CP77 RT11 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
