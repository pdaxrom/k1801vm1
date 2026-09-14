    task defaults(input [15:0] op,fps_value,guest_psw);
        begin
            scenario++;
            for(integer n=0;n<23;n++)values[n]=0;
            values[0]=op;values[1]=fps_value;values[2]=guest_psw;
            for(integer n=0;n<7;n++)values[3+n]=16'h5200+n;
            values[9]=16'o30000;
            memory[16'o4/2]=16'o1400;memory[16'o6/2]=16'o340;
            memory[16'o14/2]=16'o1600;memory[16'o16/2]=16'o340;
            memory[16'o200/2]=16'o2000;memory[16'o202/2]=16'o340;
            memory[16'o244/2]=16'o2400;memory[16'o246/2]=16'o340;
            memory[16'o30000/2-1]=16'habcd;memory[16'o30000/2-2]=16'h1234;
            delay_cycles=scenario%4;
        end
    endtask
    task same_regs(input integer count);
        for(integer n=0;n<count;n++)eq(dut.engine.dp.rf.words[n],values[3+n],"saved register");
    endtask
    task at_fetch(input [15:0] pc);
        begin
            wait(request && !bank && dut.engine.fetching && address==pc);
            @(negedge clk);
        end
    endtask
    initial begin
        for(integer n=0;n<65536;n++)memory[n]=0;
        $readmemh("build/cp70-fp11/test-image.mem",memory);
        // Full CPSW and odd USER SP survive successful register controls.
        for(integer n=0;n<16;n++)begin
            defaults(16'o170000,16'(n),16'hff00|16'(15-n));
            values[9]=16'hffff;
            prepare();finish_fp();
            same_regs(7);eq(psw,16'hff00|16'(n),"CFCC only changes NZVC");
            eq(dut.engine.dp.rf.words[7],16'o1002,"CFCC PC");
        end
        // Unsupported encodings are not misdecoded as register controls.
        for(integer n=0;n<5;n++)begin
            defaults(n==0?16'o170003:n==1?16'o170020:n==2?16'o170040:n==3?16'o171000:16'o177777,
                     16'o40017,16'o305);
            prepare();finish_fp();
            eq(memory[32768+FP_FPS/2],16'o140017,"FER despite FID");
            eq(memory[32768+FP_FEC/2],2,"unsupported opcode FEC");
            eq(memory[32768+FP_FEA/2],16'o1000,"FEA names opcode");
            same_regs(7);eq(psw,values[2],"FID preserves CPU PSW");
            eq(dut.engine.dp.rf.words[7],16'o1002,"no unsupported extension consumed");
            eq(memory[16'o30000/2-1],16'habcd,"FID does not use guest stack");
        end
        defaults(16'o170003,0,16'o305);prepare();finish_fp();
        eq(memory[32768+FP_FEC/2],2,"exception opcode code");
        eq(memory[32768+FP_FEA/2],16'o1000,"exception address");
        same_regs(6);eq(dut.engine.dp.rf.words[6],16'o27774,"FP trap stack");
        eq(memory[16'o30000/2-1],16'o305,"FP frame PSW");
        eq(memory[16'o30000/2-2],16'o1002,"FP frame PC");
        eq(dut.engine.dp.rf.words[7],16'o2400,"FP vector 244");eq(psw,16'o340,"FP vector PSW");

        defaults(16'o170001,16'o217,16'o305);prepare();
        wait(dut.engine.service_mode);@(negedge clk);
        inject_error=1;error_bank=0;error_address=16'o1000;
        finish_fp();
        same_regs(6);eq(dut.engine.dp.rf.words[7],16'o1400,"opcode reread bus-error vector");
        eq(memory[16'o30000/2-1],16'o305,"bus frame PSW");
        eq(memory[16'o30000/2-2],16'o1002,"bus frame PC");
        eq(memory[32768+FP_FPS/2],16'o217,"fault leaves FPS unchanged");
        eq(memory[32770],16'o312,"fault hook restored");

        defaults(16'o170003,0,16'o305);values[9]=16'hffff;prepare();
        allow_stopped=1;wait(stopped);@(negedge clk);
        eq(dut.engine.service_mode,1,"double fault stays in HALT bank");
        eq(memory[32768+16'o100/2],16'o1002,"terminal fault preserves CPC");

        defaults(16'o170011,0,0);prepare();
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();eq(memory[32768+FP_FPS/2],16'o200,"IRQ deferred until SETD completed");
        at_fetch(16'o2000);eq(memory[16'o30000/2-2],16'o1002,"IRQ frame PC after FP");
        eq(memory[16'o30000/2-1],0,"IRQ frame PSW");

        defaults(16'o170000,16'o11,16'o25);prepare();finish_fp();
        at_fetch(16'o1600);eq(memory[16'o30000/2-2],16'o1004,"trace after FP PC");
        eq(memory[16'o30000/2-1],16'o31,"trace sees CFCC result");

        defaults(16'o170000,16'o6,16'o23);prepare();
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();at_fetch(16'o1600);
        eq(irq_valid,1,"trace precedes queued IRQ");
        eq(memory[16'o30000/2-1],16'o26,"trace plus IRQ CFCC flags");

        // Stop request while emulating is delivered only after the FP result.
        defaults(16'o170012,0,16'o340);prepare();
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(dut.engine.service_mode);@(negedge clk);halt_button=1;
        @(negedge clk);halt_button=0;
        finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[32768+FP_FPS/2],16'o100,"debug waits for SETL result");
        eq(memory[32768+16'o100/2],16'o1002,"debug CPC after FP");
        eq(memory[32768+16'o102/2],16'o340,"debug CPSW preserved");
        same_regs(7);
        // IRQ and panel requests wait until the complete four-word result.
        defaults(16'o172622,16'o200,0);values[5]=16'o20000;
        for(integer i=0;i<4;i++)memory[16'o20000/2+i]=16'o40200+i;
        prepare();wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();
        for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],16'o40200+i,"LDD committed before IRQ");
        eq(dut.engine.dp.rf.words[2],16'o20010,"LDD autoincrement before IRQ");
        at_fetch(16'o2000);eq(memory[16'o30000/2-2],16'o1002,"LDD IRQ return PC");

        defaults(16'o172526,16'o4200,0);prepare();
        memory[16'o30000/2]=16'o100000;
        for(integer i=1;i<4;i++)memory[16'o30000/2+i]=i;
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();
        eq(dut.engine.dp.rf.words[7],16'o2400,"undefined variable vector 244");
        eq(memory[32768+FP_FPS/2],16'o104200,"undefined variable leaves CC unchanged");
        eq(memory[32768+FP_FEC/2],16'o14,"undefined variable FEC");
        eq(dut.engine.dp.rf.words[6],16'o30004,"SP delta commits before FP trap frame");
        eq(memory[16'o30004/2],16'o1002,"FP frame after operand autoincrement");
        for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+4+i],16'h8104+i,"undefined variable preserves AC");
        eq(irq_valid,1,"FP trap precedes queued IRQ");

        defaults(16'o172622,16'o200,16'o340);values[5]=16'o20000;prepare();
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(dut.engine.service_mode);@(negedge clk);halt_button=1;
        @(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],16'o40200+i,"ODT sees whole LDD result");
        eq(memory[32768+16'o100/2],16'o1002,"ODT CPC after LDD");
        $display("PASS CP70 events: %0d cases / %0d checks",scenario,checks);
        $finish;
    end
endmodule
