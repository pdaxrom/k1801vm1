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
        odt_command("S");
        check(upper(F_FPS)==16'o200,"ODT step completed SETD");
        check(upper(O_REGS+14)==G_ENTRY+2,"ODT stopped after exactly SETD");
        check(upper('o10)==F_ENTER && upper('o110)==O_ENTER,"FP/debug vectors coexist");
        odt_command("S");
        check(upper(F_FPS)==16'o300,"ODT step completed SETL");
        check(upper(O_REGS+14)==G_ENTRY+4,"ODT stopped after exactly SETL");
        odt_command("S");
        check({fram.memory[G_STATUS+1],fram.memory[G_STATUS]}==16'o300,"STFPS PC-relative memory");
        check(upper(O_REGS+14)==G_PTRSET,"STFPS consumed displacement");
        odt_command("S");check(upper(O_REGS+4)==G_BUF,"pointer setup");
        odt_command("S");check(upper(F_FPS)==16'o40000,"LDFPS immediate");
        check(upper(O_REGS+14)==G_BADFP,"LDFPS consumed immediate");
        odt_command("S");check(upper(F_FPS)==16'o140000 && upper(F_FEC)==2 && upper(F_FEA)==G_BADFP,"inhibited FP exception recorded");
        odt_command("S");
        check({fram.memory[G_BUF+1],fram.memory[G_BUF]}==2,"STST first word FEC");
        check({fram.memory[G_BUF+3],fram.memory[G_BUF+2]}==G_BADFP,"STST second word FEA");
        check(upper(O_REGS+4)==G_BUF+4,"STST increments by four");
        odt_command("S");contains("R1=140000");
        check(upper(O_REGS+14)==G_LOOP,"ODT stopped after memory status test");
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
        $display("PASS CP69 RT11 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
