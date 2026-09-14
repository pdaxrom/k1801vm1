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
        check(upper(O_REGS+14)==G_LOADD,"one SETD");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+16+2*i)=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"LDD all four words");
        check(upper(O_REGS+14)==G_BANK && upper(F_FPS)==16'o200,"LDD PC/FPS");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+40+2*i)==upper(F_ACS+16+2*i),"STD to AC5");
        odt_command("S");check(upper(F_FPS)==0,"SETF");
        odt_command("S");
        check(upper(F_ACS+16)==16'o40200 && upper(F_ACS+18)==0,"LDF immediate high half");
        check(upper(F_ACS+20)==16'o65432 && upper(F_ACS+22)==16'o23456,"LDF preserves low half");
        check(upper(O_REGS+14)==G_STORE && upper(F_FPS)==0,"immediate PC/FPS");
        odt_command("S");
        check({fram.memory[G_FRES+1],fram.memory[G_FRES]}==16'o40200 && {fram.memory[G_FRES+3],fram.memory[G_FRES+2]}==0,"STF stores two words");
        odt_command("S");check(upper(F_FPS)==16'o200,"second SETD");
        odt_command("S");
        for(integer i=0;i<4;i++)check({fram.memory[G_DRES+2*i+1],fram.memory[G_DRES+2*i]}==upper(F_ACS+16+2*i),"STD includes retained low half");
        odt_command("S");
        for(integer i=0;i<4;i++)check(upper(F_ACS+8+2*i)==upper(F_ACS+40+2*i),"LDD from AC5");
        odt_command("S");
        for(integer i=0;i<4;i++)check({fram.memory[G_BACK+2*i+1],fram.memory[G_BACK+2*i]}=={fram.memory[G_SOURCE+2*i+1],fram.memory[G_SOURCE+2*i]},"D round trip");
        check(upper(O_REGS+14)==G_LOOP,"ODT stopped after ten FP instructions");
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
        $display("PASS CP70 RT11 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
