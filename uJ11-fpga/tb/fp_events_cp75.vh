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
        $readmemh("build/cp75-fp11/test-image.mem",memory);
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
            defaults(n==0?16'o170003:n==1?16'o170020:n==2?16'o170040:n==3?16'o175000:16'o177777,
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
        // FP11-A TST executes condition codes before its UV exception.
        defaults(16'o170522,16'o4203,0);values[5]=16'o20000;
        memory[16'o20000/2]=16'o100123;
        for(integer i=1;i<4;i++)memory[16'o20000/2+i]=i;
        prepare();wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();eq(memory[32768+FP_FPS/2],16'o104214,"TST updates flags before trap");
        eq(dut.engine.dp.rf.words[2],16'o20010,"TST delta before trap");
        eq(dut.engine.dp.rf.words[7],16'o2400,"TST FP vector");
        eq(memory[16'o30000/2-2],16'o1002,"TST trap PC");
        eq(irq_valid,1,"FP trap before queued IRQ");

        defaults(16'o170622,16'o200,16'o340);values[5]=16'o20000;prepare();
        memory[16'o20000/2]=16'o140200;
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(dut.engine.service_mode);@(negedge clk);halt_button=1;
        @(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[16'o20000/2],16'o40200,"ODT sees ABSD writeback");
        eq(dut.engine.dp.rf.words[2],16'o20010,"ODT sees ABSD delta");
        eq(memory[32768+FP_FPS/2],16'o200,"ODT sees ABSD FPS");
        // Request IRQ at the first destination write of the direct AC path.
        defaults(16'o172605,16'o200,0);prepare();
        wait(request && bank && writing && address==FP_ACS+16);
        @(negedge clk);irq_valid=1;finish_fp();
        for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],16'h8114+i,"direct LDD complete before IRQ");
        eq(memory[32768+FP_FPS/2],16'o210,"direct LDD flags before IRQ");
        same_regs(7);at_fetch(16'o2000);
        eq(memory[16'o30000/2-2],16'o1002,"direct LDD IRQ PC");
        eq(memory[16'o30000/2-1],0,"direct LDD IRQ PSW");

        // Request the panel between words of direct STD; no partial AC visible.
        defaults(16'o174205,16'o207,16'o340);prepare();
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(request && bank && writing && address==FP_ACS+40);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+20+i],16'h8108+i,"ODT sees complete direct STD");
        eq(memory[32768+FP_FPS/2],16'o207,"STD leaves FPS untouched");
        eq(memory[32768+16'o100/2],16'o1002,"ODT PC after direct STD");
        eq(memory[32768+16'o102/2],16'o340,"ODT PSW after direct STD");
        same_regs(7);
        // Enabled overflow commits the wrapped result before vector 244;
        // an IRQ arriving between result words stays pending behind the trap.
        defaults(16'o172205,16'o1200,0);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h7fff:16'hffff;
            memory[32768+FP_ACS/2+20+i]=(i==0)?16'h7fff:16'hffff;
        end
        wait(request && bank && writing && address==FP_ACS+16);
        @(negedge clk);irq_valid=1;finish_fp();
        eq(memory[32768+FP_ACS/2+8],16'o177,"ADDD overflow wrapped exponent");
        for(integer i=1;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],16'hffff,"ADDD overflow fraction");
        eq(memory[32768+FP_FPS/2],16'o101206,"overflow FER/FV/FZ");
        eq(memory[32768+FP_FEC/2],16'o10,"overflow FEC");
        eq(memory[32768+FP_FEA/2],16'o1000,"overflow FEA");
        eq(dut.engine.dp.rf.words[7],16'o2400,"overflow vector before IRQ");
        eq(irq_valid,1,"IRQ queued after overflow");

        // Long cancellation is indivisible from the panel's point of view.
        defaults(16'o173205,16'o200,16'o340);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h4080:(i==3)?1:0;
            memory[32768+FP_ACS/2+20+i]=(i==0)?16'h4080:0;
        end
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(dut.engine.service_mode);@(negedge clk);halt_button=1;
        @(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[32768+FP_ACS/2+8],16'h2500,"ODT sees normalized SUBD");
        for(integer i=1;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],0,"ODT sees full normalized fraction");
        eq(memory[32768+FP_FPS/2],16'o200,"SUBD normalized FPS");
        eq(memory[32768+16'o100/2],16'o1002,"ODT PC after SUBD");

        defaults(16'o170622,16'o4200,0);values[5]=16'o20000;prepare();
        memory[16'o20000/2]=16'o100123;
        for(integer i=1;i<4;i++)memory[16'o20000/2+i]=i;
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;finish_fp();
        for(integer i=0;i<4;i++)eq(memory[16'o20000/2+i],0,"ABSD zero write before UV trap");
        eq(memory[32768+FP_FPS/2],16'o104204,"ABSD UV clears FN, sets FZ");
        eq(dut.engine.dp.rf.words[2],16'o20010,"ABSD delta commits before UV trap");
        eq(dut.engine.dp.rf.words[7],16'o2400,"ABSD UV before IRQ");
        eq(irq_valid,1,"ABSD IRQ deferred");
        // SP autoincrement moves the UV trap frame. A bus error on its
        // first write is a terminal double fault, not an oracle bus return.
        defaults(16'o170636,16'o4000,16'o340);prepare();
        memory[16'o30000/2]=16'o20000;
        memory[16'o20000/2]=16'o100123;memory[16'o20000/2+1]=16'o12345;
        wait(request && !bank && writing && address==16'o30000);
        @(negedge clk);allow_stopped=1;inject_error=1;error_bank=0;error_address=16'o30000;
        wait(stopped);@(negedge clk);
        eq(dut.engine.service_mode,1,"trap-frame bus fault remains in HALT");
        eq(memory[16'o20000/2],0,"UV operand first word committed before terminal fault");
        eq(memory[16'o20000/2+1],0,"UV operand second word committed before terminal fault");
        eq(memory[32768+FP_FPS/2],16'o104004,"UV FPS before terminal trap fault");
        // MUL double result commit is atomic with respect to a queued IRQ.
        defaults(16'o171205,16'o200,0);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h40c0:0;
            memory[32768+FP_ACS/2+20+i]=(i==0)?16'h4100:0;
        end
        wait(request && bank && writing && address==FP_ACS+16);
        @(negedge clk);irq_valid=1;finish_fp();
        eq(memory[32768+FP_ACS/2+8],16'h4140,"MULD 1.5*2 before IRQ");
        for(integer i=1;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],0,"MULD complete low words");
        eq(memory[32768+FP_FPS/2],16'o200,"MULD flags before IRQ");
        at_fetch(16'o2000);eq(memory[16'o30000/2-2],16'o1002,"MULD IRQ PC");
        // Stop in the long divide loop; ODT sees only the rounded quotient.
        defaults(16'o174605,16'o200,16'o340);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h4080:0;
            memory[32768+FP_ACS/2+20+i]=(i==0)?16'h4140:0;
        end
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(request && bank && dut.engine.fetching && address==FP_DLOOP);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[32768+FP_ACS/2+8],16'h3faa,"ODT rounded DIVD head");
        eq(memory[32768+FP_ACS/2+9],16'haaaa,"ODT DIVD word 1");
        eq(memory[32768+FP_ACS/2+10],16'haaaa,"ODT DIVD word 2");
        eq(memory[32768+FP_ACS/2+11],16'haaab,"ODT DIVD rounded last bit");
        eq(memory[32768+16'o100/2],16'o1002,"ODT PC after complete DIVD");
        // Zero divisor and FID: preserve AC/NZVC, commit source delta.
        for(integer inhibit=0;inhibit<2;inhibit++)begin
            defaults(16'o174622,inhibit?16'o40213:16'o213,0);values[5]=16'o20000;prepare();
            for(integer i=0;i<4;i++)memory[16'o20000/2+i]=0;
            wait(dut.engine.service_mode);@(negedge clk);if(!inhibit)irq_valid=1;
            finish_fp();
            for(integer i=0;i<4;i++)eq(memory[32768+FP_ACS/2+8+i],16'h8108+i,"DIV zero preserves AC");
            eq(memory[32768+FP_FPS/2],inhibit?16'o140213:16'o100213,"DIV zero preserves NZVC");
            eq(memory[32768+FP_FEC/2],4,"DIV zero FEC");
            eq(memory[32768+FP_FEA/2],16'o1000,"DIV zero FEA");
            eq(dut.engine.dp.rf.words[2],16'o20010,"DIV zero commits EA update");
            eq(dut.engine.dp.rf.words[7],inhibit?16'o1002:16'o2400,"DIV zero FID/vector");
            if(!inhibit)eq(irq_valid,1,"DIV zero exception before IRQ");
        end
        // IRQ between integer and fractional writes must see both results.
        defaults(16'o171605,16'o200,0);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h40c0:0;
            memory[32768+FP_ACS/2+20+i]=(i==0)?16'h40c0:0;
        end
        wait(request && bank && writing && address==FP_ACS+24);
        @(negedge clk);irq_valid=1;finish_fp();
        eq(memory[32768+FP_ACS/2+8],16'h3f80,"MODD fraction 0.25 before IRQ");
        eq(memory[32768+FP_ACS/2+12],16'h4100,"MODD integer 2 before IRQ");
        for(integer i=1;i<4;i++)begin
            eq(memory[32768+FP_ACS/2+8+i],0,"MODD fractional tails");
            eq(memory[32768+FP_ACS/2+12+i],0,"MODD integer tails");
        end
        at_fetch(16'o2000);eq(memory[16'o30000/2-2],16'o1002,"MODD IRQ PC");
        // AC3 source aliases the integer destination; buffer before writing.
        defaults(16'o171603,16'o200,16'o340);prepare();
        for(integer i=0;i<4;i++)begin
            memory[32768+FP_ACS/2+8+i]=(i==0)?16'h40c0:0;
            memory[32768+FP_ACS/2+12+i]=(i==0)?16'hc0c0:0;
        end
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(request && bank && dut.engine.fetching && address==FP_MMLOOP);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[32768+FP_ACS/2+8],16'hbf80,"ODT MODD signed fraction");
        eq(memory[32768+FP_ACS/2+12],16'hc100,"ODT MODD signed integer");
        eq(memory[32768+FP_FPS/2],16'o210,"ODT MODD flags from fraction");
        eq(memory[32768+16'o100/2],16'o1002,"ODT PC after whole MODD");
        // Odd AC still reports integer overflow although integer discarded.
        for(integer inhibit=0;inhibit<2;inhibit++)begin
            defaults(16'o171705,inhibit?16'o41200:16'o1200,0);prepare();
            for(integer i=0;i<4;i++)begin
                memory[32768+FP_ACS/2+12+i]=(i==0)?16'h7fff:16'hffff;
                memory[32768+FP_ACS/2+20+i]=(i==0)?16'h4100:0;
            end
            finish_fp();
            for(integer i=0;i<4;i++)begin
                eq(memory[32768+FP_ACS/2+12+i],0,"MODD overflow zero fraction");
                eq(memory[32768+FP_ACS/2+16+i],16'h8110+i,"MODD odd preserves next AC");
            end
            eq(memory[32768+FP_FPS/2],inhibit?16'o141206:16'o101206,"MODD overflow FPS");
            eq(memory[32768+FP_FEC/2],16'o10,"MODD integer overflow FEC");
            eq(dut.engine.dp.rf.words[7],inhibit?16'o1002:16'o2400,"MODD overflow FID/vector");
        end
        // Product 2 - 2**-45: integer 1, rounded F fraction becomes +/−1.
        // Both F writes must preserve their previous D low halves.
        for(integer negative=0;negative<2;negative++)begin
            defaults(16'o171605,0,16'o340);prepare();
            memory[32768+FP_ACS/2+8]=negative?16'hc080:16'h4080;
            memory[32768+FP_ACS/2+9]=1;
            memory[32768+FP_ACS/2+20]=16'h40ff;
            memory[32768+FP_ACS/2+21]=16'hfffe;
            finish_fp();
            eq(memory[32768+FP_ACS/2+8],negative?16'hc080:16'h4080,"MODF rounded fraction unity");
            eq(memory[32768+FP_ACS/2+12],negative?16'hc080:16'h4080,"MODF chopped integer unity");
            eq(memory[32768+FP_ACS/2+9],0,"MODF fractional tail");
            eq(memory[32768+FP_ACS/2+13],0,"MODF integer tail");
            for(integer i=2;i<4;i++)begin
                eq(memory[32768+FP_ACS/2+8+i],16'h8108+i,"MODF preserves fractional AC low half");
                eq(memory[32768+FP_ACS/2+12+i],16'h810c+i,"MODF preserves integer AC low half");
            end
            eq(memory[32768+FP_FPS/2],negative?8:0,"MODF flags after unity rounding");
        end
        $display("PASS CP75 events: %0d cases / %0d checks",scenario,checks);
        $finish;
    end
endmodule
