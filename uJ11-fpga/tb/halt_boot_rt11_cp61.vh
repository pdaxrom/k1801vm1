    integer cp61_prompts=0,cp61_round=0,cp61_index=0,cp61_boot_fetches=0;
    task cp61_line(input string command);
        integer j;
        begin
            cp61_prompts=prompt_count;
            for(j=0;j<command.len();j=j+1)send_byte(command[j]);send_byte(13);
            wait(prompt_count>cp61_prompts);wait(serial_chars==bus_chars);
            repeat(10000)@(negedge clk);
        end
    endtask
    task cp61_check_copy;
        integer j;
        begin
            for(j=0;j<256;j=j+1)
                if({fram.memory[65536+16384+j*2+1],fram.memory[65536+16384+j*2]}!==16'(j))
                    $fatal(1,"RT-11 HALT copy word%0d mismatch",j);
            if(dut.cpu.engine.service_ready!==0 || dut.cpu.engine.service_mode!==0)
                $fatal(1,"resident copy enabled a module or did not return");
        end
    endtask
    always @(posedge clk)if(!reset && !boot_complete && dut.acknowledge && dut.opcode_fetch &&
                            !dut.bank && dut.address==16'o4000) begin
        if(dut.bus.program_selected || !dut.bus.fram_selected)
            $fatal(1,"USER bootstrap fetched from ROM");
        cp61_boot_fetches=cp61_boot_fetches+1;
    end
    initial begin
        #1;for(cp61_index=65536;cp61_index<131072;cp61_index=cp61_index+1)fram.memory[cp61_index]=8'ha5;
        trace_rk=1;
        repeat(15)@(negedge clk);reset=0;
        wait(prompt_count==3);repeat(10000)@(negedge clk);
        cp61_line("SET SL OFF");
        cp61_line("RUN HBTEST");cp61_check_copy();
        // FRAM survives reset. Deliberately damage the installed entry; ROM
        // must reinstall it before USER starts, retaining the copied data.
        fram.memory[65536+128]=8'hff;
        cp61_prompts=prompt_count;
        @(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
        wait(prompt_count>=cp61_prompts+3);wait(serial_chars==bus_chars);
        if({fram.memory[65536+129],fram.memory[65536+128]}!==16'o020427)
            $fatal(1,"cold reset did not repair resident entry");
        cp61_check_copy();
        cp61_line("SET SL OFF");
        cp61_line("RUN HBTEST");cp61_check_copy();
        if(cp61_boot_fetches<2)$fatal(1,"missing RAM bootstrap starts");
        directory_seen=0;stage=1;
        cp61_line("DIR");
    end
