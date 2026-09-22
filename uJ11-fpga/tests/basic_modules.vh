    reg [15:0] initial_table[0:31];
    function [15:0] user_word(input integer a);
        return {fram.memory[a+1],fram.memory[a]};
    endfunction
    task module_cold;
        integer previous_prompts;
        begin
            previous_prompts=prompts;
            @(negedge clk);reset=1;repeat(15)@(negedge clk);reset=0;
            wait(prompts>=previous_prompts+3);wait(serial_chars==bus_chars);
            repeat(2000000)@(negedge clk);shell("SET SL OFF");
        end
    endtask
    task configuration(input bit fpp);
        integer rmon,config_word;
        begin
            rmon=user_word('o54);check(rmon>0 && rmon+'o300<'o160000 && !(rmon&1),"RMON pointer valid");
            config_word=user_word(rmon+'o300);
            $display("CP82 RT11 CONFIG=%06o FPP=%0d",config_word,(config_word&'o100)!=0);$fflush();
            check(((config_word&'o100)!=0)==fpp,"RT11 FPP flag follows cold module configuration");
            check(dut.cpu.engine.service_ready==(fpp?3:1) && dut.cpu.engine.debug_enabled,"ODT independent of FPP");
            for(integer i=0;i<32;i++)
                check(upper('o7000+2*i)==((i==11 && !fpp)?16'o140401:initial_table[i]),"module table preserved");
        end
    endtask
    task exception_session(input string name,input bit fpp);
        integer previous_option,previous_ready;
        begin
            @(negedge clk);fpp_count=0;fpp_entries=0;psw_accesses=0;counting=1;
            previous_option=option_count;previous_ready=ready_count;
            segment="";send_line({"RUN ",name});wait(option_count>previous_option);
            wait(serial_chars==bus_chars);send_line("A");wait(ready_count>previous_ready);
            wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);
            basic_command("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic_command("PRINT SQR(-1)");contains("?NEGATIVE SQUARE ROOT");
            basic_command("PRINT 1E30*1E30");contains("?FLOATING OVERFLOW");
            basic_command("PRINT 2+2");contains(" 4 ");
            basic_command("PRINT 1E-30*1E-30");contains("?FLOATING UNDERFLOW");
            basic_command("PRINT 1/0");contains("?DIVISION BY ZERO");
            basic_command("PRINT 2+2");contains(" 4 ");
            shell("BYE");@(negedge clk);counting=0;
            check(psw_accesses==0,"no ordinary PSW access during errors");
            check(fpp_count==fpp_entries,"every FPP status opcode uses software module");
            check((fpp_count>0)==fpp,"STST only when RT11 advertises FPP");
            $display("CP82 %s errors FPP=%0d HALT=%0d",name,fpp_count,fpp_entries);$fflush();
        end
    endtask
    initial begin
        #1;$readmemh("RETAINED",fram.memory);
        phase=1;module_cold();
        for(integer i=0;i<32;i++)initial_table[i]=upper('o7000+2*i);
        check(initial_table[8]=='o60000 && initial_table[11]=='o140407,"fixture FPP in slot two");
        configuration(1);
        module_command("OFF2");
        check(dut.cpu.engine.service_ready==0,"OFF disables live services until cold boot");
        for(integer i=0;i<32;i++)
            check(upper('o7000+2*i)==((i==11)?16'o140401:initial_table[i]),"OFF changes only chosen AUTO/status word");
        phase=2;module_cold();configuration(0);
        // Positive arithmetic is also exercised on the physical board; here
        // focus on the changed exception ABI and transitions between modules.
        exception_session("B81FIS",0);
        exception_session("B81FIJ",0);
        phase=3;oldodt=odt_prompts;
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
        wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);
        odt_command("R");contains("PSW=");
        odt_command("F");contains("?FP11 DEBUG STATE UNAVAILABLE");
        oldprompt=prompts;send_line("C");
        // The paused monitor was already waiting for a command; resume and
        // request a fresh prompt through UART rather than waiting for output.
        wait(!dut.cpu.engine.service_mode);wait(serial_chars==bus_chars);
        repeat(10000)@(negedge clk);shell("");
        phase=4;module_command("FP11");
        for(integer i=0;i<32;i++)
            check(upper('o7000+2*i)==((i==11)?16'o140403:initial_table[i]),"reinstall preserves all other records");
        phase=5;module_cold();configuration(1);
        exception_session("B81FIJ",1);
        check(rx_overruns==0,"no UART overruns");
        $display("PASS CP82 modules: %0d checks, %0d clocks, %0d UART bytes",checks,clocks,serial_chars);
        $fclose(uart_file);$finish;
    end
endmodule
