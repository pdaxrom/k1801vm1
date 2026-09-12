    integer cp61_prompts=0,cp61_index=0,cp61_boot_fetches=0;
    reg [7:0] cp61_snapshot[0:65535];
    task cp61_line(input string command);
        integer j;
        begin
            cp61_prompts=prompt_count;
            for(j=0;j<command.len();j=j+1)send_byte(command[j]);send_byte(13);
            wait(prompt_count>cp61_prompts);wait(serial_chars==bus_chars);
            repeat(10000)@(negedge clk);
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
        trace_rk=1;repeat(15)@(negedge clk);reset=0;
        wait(prompt_count==3);repeat(10000)@(negedge clk);
        cp61_line("SET SL OFF");
        for(cp61_index=0;cp61_index<65536;cp61_index=cp61_index+1)
            cp61_snapshot[cp61_index]=fram.memory[65536+cp61_index];
        cp61_line("RUN UJLOAD");
        for(cp61_index=0;cp61_index<65536;cp61_index=cp61_index+1)
            if(cp61_snapshot[cp61_index]!==fram.memory[65536+cp61_index])
                $fatal(1,"ABI-1 loader altered HALT RAM byte%o",cp61_index);
        cp61_line("RUN HBTEST");
        for(cp61_index=0;cp61_index<256;cp61_index=cp61_index+1)
            if({fram.memory[65536+16384+cp61_index*2+1],fram.memory[65536+16384+cp61_index*2]}!==16'(cp61_index))
                $fatal(1,"HALT copy word%0d mismatch",cp61_index);
        if(dut.cpu.engine.service_ready!==0 || dut.cpu.engine.service_mode!==0 || cp61_boot_fetches!=1)
            $fatal(1,"Unexpected ready/mode/boot count");
        directory_seen=0;stage=1;cp61_line("DIR HBTEST.SAV");
    end
