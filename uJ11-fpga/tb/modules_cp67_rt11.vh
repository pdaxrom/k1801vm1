    task module_command(input string command);
        integer oldloader;
        begin
            oldprompt=prompts;oldloader=loader_prompts;segment="";
            send_line("RUN UJMOD");wait(loader_prompts>oldloader);
            wait(serial_chars==bus_chars);send_line(command);
            wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
        end
    endtask
    task cold(input bit recovery);
        integer count;
        begin
            wait(serial_chars==bus_chars);count=prompts;
            @(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
            if(recovery)send_byte(8'h1b);
            wait(prompts>=count+3);wait(serial_chars==bus_chars);repeat(2000000)@(negedge clk);
            check(upper('o74)==16'(recovery),"ROM recovery selection");
            shell("SET SL OFF");
        end
    endtask
    task put_upper(input integer a,input [15:0] value);
        begin fram.memory[65536+a]=value[7:0];fram.memory[65537+a]=value[15:8];end
    endtask
    task enter_odt;
        begin
            oldodt=odt_prompts;segment="";
            @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
            wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            contains("ODT>");
        end
    endtask
    initial begin
        #1;repeat(15)@(negedge clk);reset=0;wait(prompts==3);repeat(2000000)@(negedge clk);
        phase=1;shell("SET SL OFF");module_command("STATUS");contains("0: 000000 000000 000000 000000");
        module_command("DEL0");contains("!UJMOD-I-DONE");check(upper('o7006)==0,"DEL ignores helper return value and writes zero");
        module_command("ODT");contains("!UJMOD-I-DONE");
        check(upper('o7006)=='hc103,"ODT published VALID last");check(!dut.cpu.engine.debug_enabled,"installation requires restart");
        module_command("SDBOOT");contains("!UJMOD-I-DONE");check(upper('o7016)=='hc103,"bootstrap uses identical directory record");
        phase=2;cold(0);check(dut.cpu.engine.debug_enabled,"ODT enabled by cold init");
        check(upper('o7006)=='hc107 && upper('o7016)=='hc107,"both modules initialized");
        enter_odt();odt_command("R");contains("R7=");odt_command("X");check(upper(O_RADIX)==16,"set mutable radix before reset");
        // Even reset while in ODT discards the previous session and re-enables it.
        phase=3;cold(0);check(dut.cpu.engine.debug_enabled,"ODT self-init after reset inside ODT");
        check(upper(O_RADIX)==8,"cold initializer restores octal default");
        enter_odt();send_line("C");wait(!dut.cpu.engine.service_mode);shell("");
        phase=4;module_command("HDRBAD");contains("Invalid header");check(upper('o7006)=='hc107,"bad header preserves directory");
        check(dut.cpu.engine.debug_enabled,"bad header preserves live debugger");
        module_command("CLASH");contains("overlapping");check(upper('o7006)=='hc107,"overlap preserves directory");
        module_command("PADBAD");contains("Checksum or padding failed");check(upper('o7006)=='hc102,"padding error leaves record uncommitted");
        phase=5;cold(0);check(!dut.cpu.engine.debug_enabled,"partial ODT skipped at reset");check(upper('o7016)=='hc107,"following bootstrap still initializes");
        module_command("CKBAD");contains("Checksum or padding failed");check(upper('o7006)=='hc102,"checksum error leaves record uncommitted");
        module_command("ODT");contains("!UJMOD-I-DONE");
        phase=6;cold(0);check(dut.cpu.engine.debug_enabled,"repaired module initializes");
        module_command("OFF0");contains("!UJMOD-I-DONE");check(upper('o7006)=='hc101,"OFF keeps allocation without AUTO");
        module_command("STATUS");contains("140401");
        phase=7;cold(0);check(!dut.cpu.engine.debug_enabled,"disabled module stays disabled");
        module_command("DEL0");contains("!UJMOD-I-DONE");check(upper('o7006)==0,"DEL forgets record without erasing code");
        // Interrupt actual native payload copying, not a model of the loader.
        phase=8;oldprompt=prompts;oldloader_saved=loader_prompts;
        send_line("RUN UJMOD");wait(loader_prompts>oldloader_saved);wait(serial_chars==bus_chars);send_line("ODT");
        wait(dut.acknowledge && dut.writing && dut.bank && dut.address==16'o10100);
        wait(serial_chars==bus_chars);check(upper('o7006)=='hc102,"record invalid during payload transfer");
        cold(0);check(!dut.cpu.engine.debug_enabled,"reset mid-copy cannot activate partial image");
        check(upper('o7016)=='hc107,"reset mid-copy preserves other record");
        module_command("ODT");contains("!UJMOD-I-DONE");
        // Simulated retained-code corruption with a matching checksum: a hung
        // initializer cannot stop ESC recovery, because no FRAM call occurs.
        phase=9;put_upper('o6000,16'o777);put_upper('o7012,1);put_upper('o7014,16'o777);
        cold(1);check(!dut.cpu.engine.debug_enabled,"ESC skips even the preceding valid ODT");
        check(upper('o7016)=='hc107,"forced recovery does not call or rewrite module records");
        module_command("DEL1");contains("!UJMOD-I-DONE");module_command("SDBOOT");contains("!UJMOD-I-DONE");
        phase=10;cold(0);check(dut.cpu.engine.debug_enabled,"normal boot restored after recovery repair");
        check(upper('o7006)=='hc107 && upper('o7016)=='hc107,"repaired generic directory");
        enter_odt();odt_command("D");contains("ODT>");
        $display("PASS CP67 RT11 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
