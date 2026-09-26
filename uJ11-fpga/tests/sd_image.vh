    task await_text(input string value);
        bit found;
        begin
            found=0;
            while(!found) begin
                @(negedge clk);
                if(segment.len()>=value.len())
                    found=segment.substr(segment.len()-value.len(),segment.len()-1)==value;
            end
            wait(serial_chars==bus_chars);
        end
    endtask
    string memory_before;
    initial begin
        #1;repeat(15)@(negedge clk);reset=0;
        await_text("FRAM modules are not changed by this disk.");
        oldprompt=prompts;wait(prompts>oldprompt);wait(serial_chars==bus_chars);
        contains("RT-11FB");
        check(!dut.cpu.engine.debug_enabled,"disk does not install ODT silently");
        check(dut.cpu.engine.service_ready==0,"blank FRAM has no optional services");
        phase=1;
        shell("TYPE SY:CLOCK.TXT");contains("FRUN SY:CLOCK");contains("UNLOAD F");
        shell("DIR SY:*.BIN");contains("ODT");contains("FP11");contains("SDBOOT");
        shell("INSTALL HG");check(segment.len()<60,"HG installs on real GPIO CSR");
        shell("LOAD HG");check(segment.len()<60,"packaged HG loads without host access");
        shell("UNLOAD HG");
        oldprompt=prompts;segment="";send_line("RUN SY:UJMOD");await_text("UJMOD> ");
        send_line("STATUS");wait(prompts>oldprompt);wait(serial_chars==bus_chars);
        contains("0: 000000 000000 000000 000000");
        contains("7: 000000 000000 000000 000000");
        phase=2;
        segment="";send_line("RUN SY:B81FIJ");await_text("?");
        segment="";send_line("A");await_text("READY\r\n");
        segment="";send_line("PRINT 2+2");await_text("READY\r\n");contains(" 4");
        segment="";send_line("PRINT 1/0");await_text("READY\r\n");contains("DIVISION BY ZERO");
        segment="";send_line("PRINT 2+2");await_text("READY\r\n");contains(" 4");
        shell("BYE");
        phase=3;
        shell("SHOW MEMORY");memory_before=segment;
        oldprompt=prompts;segment="";send_line("FRUN SY:CLOCK");
        await_text("UART Q OR ESC TO EXIT");
        // FRUN's banner precedes the background KMON prompt. Drain that
        // prompt before sending DIR, otherwise shell() could accept it as
        // the completion of the next command while DIR still runs.
        wait(prompts>oldprompt);wait(serial_chars==bus_chars);
        wait(display_frames>0);
        send_byte(2);repeat(100000)@(negedge clk);
        shell("DIR SY:CLOCK.REL");contains("1 Files, 5 Blocks");
        shell("SHOW MEMORY");contains("CLOCK");contains("1007.");
        check(display_frames>0,"actual HCMS output while foreground CLOCK runs");
        // FG .EXIT returns console ownership with B>, but does not emit a
        // fresh KMON dot when BG was already sitting at its prompt.
        segment="";send_byte(6);send_byte("Q");await_text("B>\r\n");
        send_byte(2);repeat(100000)@(negedge clk);
        shell("UNLOAD F");
        check(segment.len()<60,"foreground unload completes without error");
        shell("SHOW MEMORY");
        check(segment==memory_before,"UNLOAD F restores the original memory map");
        shell("TIME");contains(":");
        check(card.read_count>0,"real SPI SD sectors read");
        check(serial_chars==bus_chars,"UART waveforms agree");
        $display("PASS SD image: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
