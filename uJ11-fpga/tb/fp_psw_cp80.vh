    // A CPU-local PSW operand must never be sent to the external memory bus.
    always @(posedge clk) if(!reset && request && !bank && address==16'o177776)
        $fatal(1,"case%0d PSW escaped to external bus",scenario);
    task defaults(input [15:0] op,fps_value,guest_psw);
        begin
            scenario++;guest_start=16'o1000;
            for(integer n=0;n<23;n++)values[n]=0;
            values[0]=op;values[1]=fps_value;values[2]=guest_psw;
            for(integer n=0;n<7;n++)values[3+n]=16'h5200+n;
            values[9]=16'o30000;
            memory[16'o4/2]=16'o1400;memory[16'o6/2]=16'o340;
            memory[16'o14/2]=16'o1600;memory[16'o16/2]=16'o340;
            memory[16'o200/2]=16'o2000;memory[16'o202/2]=16'o340;
            memory[16'o244/2]=16'o2400;memory[16'o246/2]=16'o340;
            memory[16'o30000/2-1]=16'habcd;memory[16'o30000/2-2]=16'h1234;
            memory[16'o177776/2]=16'hdead;
            delay_cycles=scenario%4;
        end
    endtask
    task ac0(input [63:0] bits);
        for(integer n=0;n<4;n++)memory[32768+FP_ACS/2+n]=16'(bits>>(48-16*n));
    endtask
    task status(input [15:0] want_psw,want_fps,want_pc);
        begin
            eq(psw,want_psw,"USER PSW");
            eq(memory[32768+FP_FPS/2],want_fps,"FPS independently committed");
            eq(dut.engine.dp.rf.words[7],want_pc,"PC");
            eq(memory[16'o177776/2],16'hdead,"external PSW poison untouched");
            eq(memory[32770],16'o312,"HALT fault hook restored");
        end
    endtask
    task at_fetch(input [15:0] pc);
        begin wait(request && !bank && dut.engine.fetching && address==pc);@(negedge clk);end
    endtask
    reg [15:0] p,f,w,expected_reg;
    initial begin
        for(integer n=0;n<65536;n++)memory[n]=0;
        $readmemh("PSW_IMAGE",memory);
        // Walking bits, all NZVC combinations and mixed high bits, both T
        // states. These expectations are independent architectural masks.
        for(integer n=0;n<288;n++)begin
            p=(n<32)?(16'(1<<(n/2))|16'((n%2)*16)):(16'((n-32)*257));
            defaults(16'o170112,16'o140217,p);values[5]=16'o177776;
            prepare();finish_fp();
            status(p,p&16'o144757,16'o1002+(p[4]?2:0));
            eq(dut.engine.dp.rf.words[2],16'o177776,"LDFPS leaves address register");

            f=((n<32)?(16'(1<<(n/2))|16'((n%2)*16)):16'((n-32)*257))&16'o147757;
            p=(n%2)?16'o173777:16'o405;
            defaults(16'o170212,f,p);values[5]=16'o177776;
            prepare();finish_fp();w=(f&~16'o3020)|(p&16'o20);
            status(w,f,16'o1002+(p[4]?2:0));
            for(integer r=0;r<7;r++)eq(dut.engine.dp.rf.words[r],values[3+r],"mode/RS are CP67 metadata");
        end
        // All non-register EA modes, each CPU register including SP. The
        // R7 forms follow below because PC also owns the extension stream.
        for(integer store_op=0;store_op<2;store_op++)
        for(integer mode=1;mode<8;mode++)for(integer regno=0;regno<7;regno++)begin
            defaults((store_op?16'o170200:16'o170100)+16'(mode*8+regno),16'o12345,16'o345);
            case(mode)
                1,2:values[3+regno]=16'o177776;
                3:begin values[3+regno]=16'o20000;memory[16'o20000/2]=16'o177776;end
                4:values[3+regno]=0;
                5:begin values[3+regno]=16'o20002;memory[16'o20000/2]=16'o177776;end
                6:values[3+regno]=16'o177772;
                7:begin values[3+regno]=16'o177772;memory[0]=16'o177776;end
            endcase
            expected_reg=values[3+regno];
            if(mode==2 || mode==3)expected_reg=expected_reg+2;
            if(mode==4 || mode==5)expected_reg=expected_reg-2;
            prepare();
            if(mode==6)memory[16'o1002/2]=4;
            if(mode==7)memory[16'o1002/2]=6;
            finish_fp();
            status(store_op?16'o10345:16'o345,store_op?16'o12345:16'o345,(mode>=6)?16'o1004:16'o1002);
            for(integer r=0;r<7;r++)eq(dut.engine.dp.rf.words[r],(r==regno)?expected_reg:values[3+r],"EA register result");
        end
        // PC absolute, relative, relative deferred, immediate and extension
        // at PSW. No synthetic bus PSW mapping is supplied to the CPU.
        defaults(16'o170137,0,16'o345);prepare();memory[16'o1002/2]=16'o177776;
        finish_fp();status(16'o345,16'o345,16'o1004);
        defaults(16'o170167,0,16'o345);prepare();memory[16'o1002/2]=16'o176772;
        finish_fp();status(16'o345,16'o345,16'o1004);
        defaults(16'o170177,0,16'o345);prepare();memory[16'o1002/2]=16'o17000-16'o1004;
        memory[16'o17000/2]=16'o177776;finish_fp();status(16'o345,16'o345,16'o1004);
        defaults(16'o170127,0,16'o345);guest_start=16'o177774;prepare();
        finish_fp();status(16'o345,16'o345,0);
        defaults(16'o170162,0,16'o344);guest_start=16'o177774;values[5]=16'o177776-16'o344;
        prepare();finish_fp();status(16'o344,16'o344,0);
        // PSW as an indirect pointer must use saved USER PSW, not HALT 0340.
        defaults(16'o170132,0,16'o4000);values[5]=16'o177776;prepare();memory[16'o4000/2]=16'o12345;
        finish_fp();status(16'o4000,16'o2345,16'o1002);
        eq(dut.engine.dp.rf.words[2],0,"deferred pointer wraps increment");
        defaults(16'o170152,0,16'o4000);values[5]=0;prepare();memory[16'o4000/2]=16'o12345;
        finish_fp();status(16'o4000,16'o2345,16'o1002);
        eq(dut.engine.dp.rf.words[2],16'o177776,"deferred pointer predecrement");
        // Integer conversions: explicit NZVC differs from conversion NZVC.
        // +1 stores PSW.C=1 but conversion FPS.NZVC=0; -1 writes NZVC=17
        // but FPS.NZVC=N. STEXP +1 has the same priority requirement.
        for(integer n=0;n<4;n++)begin
            defaults(n==3?16'o175012:16'o175412,16'o200,16'o345);values[5]=16'o177776;
            prepare();
            case(n)
                0,3:begin ac0(64'h4080000000000000);w=1;f=16'o200;end
                1:begin ac0(64'hc080000000000000);w=16'o174757;f=16'o210;end
                2:begin ac0(0);w=0;f=16'o204;end
            endcase
            finish_fp();status(w,f,16'o1002);
        end
        // Read signed integer from PSW, including zero reserved bits.
        defaults(16'o177012,0,16'o3001);values[5]=16'o177776;prepare();finish_fp();
        status(16'o3001,0,16'o1002);eq(memory[32768+FP_ACS/2],16'h4080,"LDCIF PSW equals +1");
        // Multiword transfers cross the PSW address, and wrap to address 0.
        for(integer d=0;d<2;d++)for(integer n=0;n<(d?4:2);n++)begin
            defaults(16'o174022,d?16'o200:0,16'o345);values[5]=16'o177776-16'(2*n);
            prepare();ac0(64'h12345678345a789c);finish_fp();
            w=16'((64'h12345678345a789c>>(48-16*n)))&~16'o3020;
            status(w,d?16'o200:0,16'o1002);
            eq(dut.engine.dp.rf.words[2],values[5]+(d?8:4),"STF/STD full increment");
            for(integer j=0;j<(d?4:2);j++)if(j!=n)
                eq(memory[(16'(values[5]+2*j))>>1],16'(64'h12345678345a789c>>(48-16*j)),"multiword ordinary destination");
        end
        defaults(16'o172412,16'o200,16'o40340);values[5]=16'o177776;prepare();
        memory[0]=16'h1234;memory[1]=16'h5678;memory[2]=16'h9abc;finish_fp();
        status(16'o40340,16'o200,16'o1002);
        eq(memory[32768+FP_ACS/2],16'o40340,"LDD leading PSW word");
        eq(memory[32768+FP_ACS/2+3],16'h9abc,"LDD wraps through zero");
        // STST FEC at PSW, FEA at 0; no external write at PSW.
        defaults(16'o170322,0,16'o345);values[5]=16'o177776;prepare();
        memory[32768+FP_FEC/2]=6;memory[32768+FP_FEA/2]=16'o123456;finish_fp();
        status(6,0,16'o1002);eq(memory[0],16'o123456,"STST FEA wraps");
        eq(dut.engine.dp.rf.words[2],2,"STST increments four");
        // Long conversion: PSW can be either high or low word. FPS reflects
        // the whole signed long, never the low nibble of the written PSW.
        defaults(16'o175422,16'o300,16'o345);values[5]=16'o177774;prepare();
        ac0(64'h4100000000000000);finish_fp();status(2,16'o300,16'o1002);
        eq(memory[16'o177774/2],0,"long high word");
        defaults(16'o175422,16'o300,16'o345);values[5]=16'o177776;prepare();
        ac0(64'h4100000000000000);finish_fp();status(0,16'o300,16'o1002);
        eq(memory[0],2,"long low word wraps");
        // Unary read/modify/write shares the PSW alias and preserves T.
        for(integer n=0;n<3;n++)begin
            defaults(n==0?16'o170412:(n==1?16'o170612:16'o170712),0,16'o140345);values[5]=16'o177776;
            prepare();memory[0]=16'h1234;finish_fp();
            // CLR, ABS, NEG. Both ABS and NEG make this negative input positive.
            status(n==0?0:16'o40345,n==0?4:0,16'o1002);
        end
        // Operand read succeeds at PSW, then the next word faults. No AC,
        // FPS or deferred address-register update may commit.
        defaults(16'o172422,16'o207,16'o40340);values[5]=16'o177776;prepare();
        inject_error=1;error_bank=0;error_address=0;finish_fp();
        status(16'o340,16'o207,16'o1400);
        eq(memory[16'o30000/2-1],16'o40340,"PSW read does not change fault context");
        eq(dut.engine.dp.rf.words[2],16'o177776,"read fault keeps deferred R2");
        for(integer n=0;n<4;n++)eq(memory[32768+FP_ACS/2+n],16'h8100+n,"read fault preserves AC");
        // STEXP of zero writes -128: CPU flags come from the PSW word,
        // while the FPS receives the sign of the signed exponent.
        defaults(16'o175012,16'o200,16'o345);values[5]=16'o177776;prepare();ac0(0);
        finish_fp();status(16'o174600,16'o210,16'o1002);
        // Absolute pointer fetched from PSW at the end of the PC stream.
        defaults(16'o170137,0,16'o4000);guest_start=16'o177774;prepare();
        memory[16'o4000/2]=16'o12345;finish_fp();status(16'o4000,16'o2345,0);
        // Partial stores keep a prior PSW write on a subsequent bus error;
        // CPU/FPS calculated flags and Rn increment do not commit on fault.
        defaults(16'o175422,16'o317,16'o345);values[5]=16'o177776;prepare();
        ac0(64'h4100000000000000);inject_error=1;error_bank=0;error_address=0;
        finish_fp();status(16'o340,16'o317,16'o1400);
        eq(memory[16'o30000/2-1],0,"fault frame sees prior explicit PSW store");
        eq(dut.engine.dp.rf.words[2],16'o177776,"failed long keeps R2");
        defaults(16'o175422,16'o317,16'o345);values[5]=16'o177774;prepare();
        ac0(64'h4100000000000000);inject_error=1;error_bank=0;error_address=16'o177774;
        finish_fp();status(16'o340,16'o317,16'o1400);
        eq(memory[16'o30000/2-1],16'o345,"fault before PSW leaves it intact");
        // Odd operands trap instead of aliasing PSW, for read and write.
        for(integer n=0;n<2;n++)begin
            defaults(n?16'o170212:16'o170112,16'o200,16'o345);values[5]=16'o177777;
            prepare();finish_fp();status(16'o340,16'o200,16'o1400);
            eq(memory[16'o30000/2-1],16'o345,"odd operand frame PSW");
        end
        // The explicit-write marker belongs to one instruction only.
        defaults(16'o170212,1,16'o345);values[5]=16'o177776;prepare();
        ac0(64'h4080000000000000);memory[16'o1002/2]=16'o175000;
        finish_fp();status(1,1,16'o1002);
        finish_fp();status(0,0,16'o1004);eq(dut.engine.dp.rf.words[0],1,"following STEXP restores ordinary CPU flags");
        // Conversion overflow at PSW: explicit zero wins CPU.Z/C while FPS
        // records Z/C/FER; a following FP trap stacks that explicit zero.
        for(integer n=0;n<2;n++)begin
            defaults(16'o175412,n?16'o400:16'o40400,16'o345);values[5]=16'o177776;
            prepare();ac0(64'h7f80000000000000);finish_fp();
            status(n?16'o340:0,n?16'o100405:16'o140405,n?16'o2400:16'o1002);
            eq(memory[32768+FP_FEC/2],6,"conversion error FEC");
            if(n)eq(memory[16'o30000/2-1],0,"FP exception frame uses explicitly written PSW");
        end
        // An explicit write cannot clear T. Trace precedes an already queued
        // interrupt and its frame contains the just-written USER PSW.
        defaults(16'o170212,1,16'o365);values[5]=16'o177776;prepare();
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();status(16'o21,1,16'o1004);at_fetch(16'o1600);
        eq(memory[16'o30000/2-1],16'o21,"trace explicit PSW");eq(irq_valid,1,"trace precedes IRQ");
        // Lowering priority takes effect after completion, not during service.
        defaults(16'o170212,0,16'o340);values[5]=16'o177776;prepare();
        wait(dut.engine.service_mode);@(negedge clk);irq_valid=1;
        finish_fp();status(0,0,16'o1002);at_fetch(16'o2000);
        eq(memory[16'o30000/2-1],0,"IRQ sees new priority and flags");
        // ODT sees completed explicit PSW and complete operand update.
        defaults(16'o170222,16'o401,16'o345);values[5]=16'o177776;prepare();
        memory[32768+16'o110/2]=16'o3600;memory[32768+16'o112/2]=16'o340;
        memory[32768+16'o3600/2]=16'o777;
        wait(dut.engine.service_mode);@(negedge clk);halt_button=1;
        @(negedge clk);halt_button=0;finish_fp();
        wait(request && bank && dut.engine.fetching && address==16'o3600);@(negedge clk);
        eq(memory[32768+16'o102/2],16'o401,"ODT saved PSW");
        eq(dut.engine.dp.rf.words[2],0,"ODT sees autoincrement");
        $display("PASS CP80 PSW: %0d cases / %0d checks ROM_DECODE=%0d",scenario,checks,ROM_DECODE);
        $finish;
    end
endmodule
