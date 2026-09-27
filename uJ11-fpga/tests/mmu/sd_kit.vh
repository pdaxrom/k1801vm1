// Published image validation through the actual SD/SRAM/UART interfaces.
    integer timer_lines=0,high_writes=0,high_reads=0;
    always @(posedge clk) if(dut.request && dut.ready)begin
        if(dut.address>=22'h40000 && dut.address<22'h200000)begin
            if(dut.writing)high_writes++;else high_reads++;
        end
        if(dut.writing && dut.address==22'o17777566 && previous_char==10 && dut.write_data[7:0]=="T")
            timer_lines++;
    end
    task manual(input string name);
        begin shell({"TYPE ",name,".TXT"});contains({"END ",name,".TXT"});end
    endtask
    task test_sd_kit;
        integer oldlines,oldprompts;
        string timer_command;
        begin
            shell("SHOW CONFIGURATION");contains("2048KB of memory");contains("22 bit addressing is on");
            shell("SHOW MEMORY");contains("10000000  MEMTOP");
            manual("README");manual("BASIC");manual("ODT");manual("MEMORY");
            manual("TIMER");manual("HOST");manual("FILES");manual("BUILD");
            shell("LOAD HG");shell("UNLOAD HG");
            oldlines=timer_lines;oldprompts=prompts;segment="";
            timer_command="RUN TMRATE";
            for(integer i=0;i<timer_command.len();i++)send_byte(timer_command[i]);send_byte(13);
            wait(timer_lines>=oldlines+2);repeat(200000)@(negedge clk);
            contains("CFG ");send_byte("Q");wait(prompts>oldprompts);wait(serial_chars==bus_chars);
            shell("COPY README.TXT XMTEST.TXT");
            shell("DIFFERENCES/BINARY README.TXT XMTEST.TXT");contains("No differences found");
            shell("DELETE/NOQUERY XMTEST.TXT");
            shell("INITIALIZE/NOQUERY VM:");
            shell("COPY RT11XM.SYS VM:MMUTST.SYS");
            shell("DIFFERENCES/BINARY RT11XM.SYS VM:MMUTST.SYS");contains("No differences found");
            shell("UNPROTECT VM:MMUTST.SYS");shell("DELETE/NOQUERY VM:MMUTST.SYS");
        end
    endtask
