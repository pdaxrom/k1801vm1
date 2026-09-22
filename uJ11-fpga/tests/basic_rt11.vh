    integer ready_count=0,option_count=0;
    integer fis_count=0,fpp_count=0,fpp_entries=0,psw_accesses=0;
    integer fis_ops[0:3];
    bit counting=0;
    longint start_clocks;
    always @(posedge clk) if(!reset) begin
        if(dut.acknowledge && dut.writing && dut.bus.uart_selected && dut.address==16'o177566) begin
            if({window[39:0],dut.data[7:0]}==48'h52454144590d)ready_count<=ready_count+1;
            if({window[39:0],dut.data[7:0]}=="DUAL)?")option_count<=option_count+1;
        end
        if(counting && dut.acknowledge) begin
            if(!dut.bank && dut.opcode_fetch) begin
                if((dut.rdata & 16'o177740)==16'o075000)begin
                    fis_count<=fis_count+1;
                    fis_ops[dut.rdata[4:3]]<=fis_ops[dut.rdata[4:3]]+1;
                end
                if((dut.rdata & 16'o170000)==16'o170000)fpp_count<=fpp_count+1;
            end
            if(dut.bank && dut.opcode_fetch && dut.address==F_ENTER)fpp_entries<=fpp_entries+1;
            if(!dut.bank && dut.address==16'o177776)psw_accesses<=psw_accesses+1;
        end
    end
    task basic_command(input string command);
        integer previous_ready;
        begin
            previous_ready=ready_count;segment="";send_line(command);
            wait(ready_count>previous_ready);wait(serial_chars==bus_chars);
            repeat(10000)@(negedge clk);
        end
    endtask
    task test_basic(input string name,input bit double_precision,input bit fis);
        integer previous_option,previous_ready;
        begin
            @(negedge clk);fis_count=0;fpp_count=0;fpp_entries=0;psw_accesses=0;
            for(integer i=0;i<4;i++)fis_ops[i]=0;
            counting=1;start_clocks=clocks;
            previous_option=option_count;previous_ready=ready_count;
            segment="";send_line({"RUN ",name});wait(option_count>previous_option);
            wait(serial_chars==bus_chars);send_line("A");wait(ready_count>previous_ready);
            wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            basic_command("RUN B81TST");contains("CHECKS= 29  ERRORS= 0");contains("CP81 PASS");
            if(double_precision)begin basic_command("RUN B81DBL");contains("CP81 DOUBLE PASS");end
            shell("BYE");@(negedge clk);counting=0;
            if(fis)begin
                for(integer i=0;i<4;i++)check(fis_ops[i]>0,"every FIS arithmetic opcode exercised");
                check(fpp_count==0 && fpp_entries==0,"FIS build uses no FPP assist");
            end else check(fpp_count>0 && fpp_entries==fpp_count,"all FPP opcodes enter software assist");
            check(psw_accesses==0,"BASIC session needs no ordinary mapped PSW");
            $display("CP81 BASIC %s clocks=%0d FIS=%0d FADD=%0d FSUB=%0d FMUL=%0d FDIV=%0d FPP=%0d HALT=%0d PSW=%0d",
                name,clocks-start_clocks,fis_count,fis_ops[0],fis_ops[1],fis_ops[2],fis_ops[3],fpp_count,fpp_entries,psw_accesses);
            $fflush();
        end
    endtask
    task module_command(input string command);
        integer oldloader;
        begin
            oldprompt=prompts;oldloader=loader_prompts;segment="";
            send_line("RUN UJMOD");wait(loader_prompts>oldloader);
            wait(serial_chars==bus_chars);send_line(command);
            wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            contains("!UJMOD-I-DONE");
        end
    endtask
    initial begin
        #1;repeat(15)@(negedge clk);reset=0;wait(prompts==3);repeat(2000000)@(negedge clk);
        phase=1;shell("SET SL OFF");
        module_command("ODT");module_command("SDBOOT");module_command("FP11");
        phase=2;oldprompt=prompts;@(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
        wait(prompts>=oldprompt+3);wait(serial_chars==bus_chars);repeat(2000000)@(negedge clk);
        shell("SET SL OFF");
        check(dut.cpu.engine.service_ready==3 && dut.cpu.engine.debug_enabled,"modules cold initialized");
        phase=3;test_basic("B81FIS",0,1);
        phase=4;test_basic("B81FPU",0,0);
        phase=5;test_basic("B81FPD",1,0);
        check(rx_overruns==0,"no UART overrun");
        $display("PASS CP81 BASIC: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
