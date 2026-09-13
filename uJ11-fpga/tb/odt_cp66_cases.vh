memory['o30000/2]=16'o004767;memory['o30002/2]=16'o000014;
memory['o30004/2]=16'o000240;memory['o30006/2]=16'o000777;
memory['o30020/2]=16'o005201;memory['o30022/2]=16'o004767;
memory['o30024/2]=16'o000012;memory['o30026/2]=16'o005202;
memory['o30030/2]=16'o000207;memory['o30040/2]=16'o005203;
memory['o30042/2]=16'o000207;
memory['o30200/2]=0;memory['o30202/2]=16'o000777;
memory['o30300/2]=3;memory['o30302/2]=16'o005204;memory['o30304/2]=16'o000777;
memory['o14/2]=16'o30400;memory['o16/2]=16'o340;
memory['o30400/2]=16'o005237;memory['o30402/2]=16'o30420;memory['o30404/2]=2;
memory['o100/2]=16'o30500;memory['o102/2]=16'o340;
memory['o30500/2]=16'o005237;memory['o30502/2]=16'o30520;memory['o30504/2]=2;
memory['o31000/2]=16'o004767;memory['o31002/2]=16'o000074;
memory['o31004/2]=16'o000240;memory['o31100/2]=1;memory['o31102/2]=16'o000207;
memory['o31300/2]=16'o005200;memory['o31302/2]=16'o000240;memory['o31304/2]=16'o000777;
memory['o32020/2]=16'o005305;memory['o32022/2]=16'o001403;
memory['o32024/2]=16'o004767;memory['o32026/2]=16'o177770;
memory['o32030/2]=16'o005203;memory['o32032/2]=16'o000207;
memory['o30600/2]=16'o012737;memory['o30602/2]=16'o005203;
memory['o30604/2]=16'o31000;memory['o30606/2]=16'o000777;
memory['o30776/2]=16'o000240;
repeat(4)@(negedge clk);reset=0;wait(dut.engine.debug_enabled);
@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts==1);
contains("UJ11 ODT CP66");command("P 20002");contains("BP 020002");
eq(memory['o20002/2],16'o5202,"P does not patch while stopped");
command("C");contains("BREAKPOINT");eq(upper(O_REGS+14),16'o20002,"stop before original instruction");
eq(upper(O_REGS+2),16'h1235,"previous instruction executed");eq(upper(O_REGS+4),16'h102,"original instruction not executed");
eq(upper(O_REGS+8),16'o125061,"breakpoint wins over R4 loader signature");eq(upper(O_REGS+12),16'hffff,"odd SP is untouched");
eq(upper(O_REGS),16'h5555,"R0 preserved");eq(upper(O_REGS+6),16'h2468,"R3 preserved");eq(upper(O_REGS+10),16'o30000,"R5 preserved");
eq(upper(O_REGS+16),1,"flags preserved across software hook");
eq(upper('o170),16'o200,"native vector restored on stop");eq(memory['o20002/2],16'o5202,"patch restored before prompt");
command("P 20004");command("C");contains("BREAKPOINT");eq(upper(O_REGS+14),16'o20004,"stop at next breakpoint after internal step");
eq(upper(O_REGS+4),16'h103,"continue executes original exactly once");
command("Z");contains("BREAKPOINTS CLEARED");command("U 20006");contains("RUN TO ADDRESS");
eq(upper(O_REGS+14),16'o20006,"temporary run-to PC");eq(upper(O_BPTEMP+4),0,"temporary removed");
command("R 6 150000");command("R 7 30000");command("T");contains("OVER RETURN");
eq(upper(O_REGS+14),16'o30004,"nested JSR step-over target");eq(upper(O_REGS+12),16'o150000,"nested JSR restores SP");
// A genuine loader-signature HALT must still take the original resident path.
command("P 20000");command("R 1 0");command("R 2 0");command("R 4 125061");command("R 7 30200");
baseprompt=prompts;segment="";oldfetches=guest_fetches;send({"C",8'd13});
wait(guest_fetches>oldfetches+4);eq(16'(prompts),16'(baseprompt),"foreign loader call does not stop in ODT");
@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts>baseprompt);
eq(upper(O_REGS),1,"original resident range failure returned");eq(memory['o20000/2],16'o5201,"loader call disarmed patches");
eq(upper('o170),16'o200,"loader restored vector");command("Z");
// Guest BPT remains an ordinary RT-11-style trap, without vector interception.
command("R 7 30300");command("U 30304");contains("RUN TO ADDRESS");
eq(memory['o30420/2],1,"guest BPT handler executed");eq(memory['o14/2],16'o30400,"BPT vector unchanged");
eq(upper(O_REGS+12),16'o150000,"BPT stack frame unwound normally");
// STEP OVER continues through WAIT and receives an ordinary interrupt.
command("R 7 31000");command("R 10 0");baseprompt=prompts;segment="";send({"T",8'd13});
wait(guest_wait && !dut.engine.service_mode);repeat(1000)@(negedge clk);
@(negedge clk);irq_req=1;wait(prompts>baseprompt);
contains("OVER RETURN");eq(16'(irq_count),1,"ordinary IRQ accepted during step-over");
eq(memory['o30520/2],1,"IRQ handler ran");eq(upper(O_REGS+14),16'o31004,"WAIT/IRQ callee returned");
eq(upper(O_REGS+12),16'o150000,"IRQ and JSR stack frames balanced");
// Several recursive calls reach the same return address at different depths.
command("R 7 32024");command("R 3 0");command("R 5 3");command("T");contains("OVER RETURN");
eq(upper(O_REGS+14),16'o32030,"recursive return PC");eq(upper(O_REGS+12),16'o150000,"recursive original SP");
eq(upper(O_REGS+6),2,"inner returns did not stop the outer STEP OVER");
// Trace belongs to the guest and shares no monitor vector hook.
memory['o30404/2]=6;memory['o30420/2]=0;
command("R 7 31300");command("R 10 20");command("U 31304");contains("RUN TO ADDRESS");
eq(memory['o30420/2],2,"guest trace handler ran after both instructions");eq(memory['o14/2],16'o30400,"trace vector unchanged");
eq(upper(O_REGS+16)&16'o20,16'o20,"guest T bit preserved");
command("S");eq(upper(O_META)&16'o100,16'o100,"single-step preserves deferred guest trace");
command("R 10 0");command("R 7 20006");
// Bounds, HALT targets, capacity, duplicate SET, and individual CLEAR.
command("P 777");contains("?SYNTAX");command("P 20001");contains("?SYNTAX");
command("P 177562");contains("?SYNTAX");command("U 30200");contains("?BP");eq(upper(O_BPTEMP+4),0,"failed temporary leaves no definition");
command("P 20000");command("P 20002");command("P 20004");command("P 30004");command("P 30006");contains("?BP");
command("P 20000");contains("BP 020000");command("P");contains("BP 020002");
command("Z 20002");eq(upper(O_BPTAB+6+4),0,"individual point cleared");
memory[(65536+'o170)/2]=16'o1234;oldfetches=guest_fetches;
command("C");contains("?BP");eq(16'(guest_fetches),16'(oldfetches),"unknown native hook blocks execution");
eq(upper('o170),16'o1234,"unknown hook not overwritten");memory[(65536+'o170)/2]=16'o200;command("Z");
// Failed arming rolls back earlier patches and leaves guest execution stopped.
command("P 20000");command("P 31000");oldfetches=guest_fetches;
fail_write=1;command("C");contains("?BP");fail_write=0;
eq(memory['o20000/2],16'o5201,"rollback restores earlier point after write fault");eq(memory['o31000/2],16'o004767,"failed write preserves target");
eq(16'(guest_fetches),16'(oldfetches),"arming fault cannot run guest");
drop_write=1;command("C");contains("?BP");drop_write=0;
eq(memory['o20000/2],16'o5201,"rollback after readback mismatch");eq(upper('o170),16'o200,"no live hook after arming failure");
inject=1;command("C");contains("?BP");inject=0;command("Z");
// Actual guest self-modification at a patched word must survive disarming.
command("P 31000");command("R 7 30600");command("U 30606");contains("?BP");
eq(memory['o31000/2],16'o005203,"changed guest opcode is not overwritten");eq(upper(O_BPTAB+4),0,"modified point disabled");
// The original instruction can change its own opcode during the hidden step.
memory['o31000/2]=16'o012737;memory['o31002/2]=16'o005201;
memory['o31004/2]=16'o31000;memory['o31006/2]=16'o000777;
command("P 31000");command("R 7 31000");command("C");contains("?BP");
eq(memory['o31000/2],16'o005201,"self-modification during original STEP retained");eq(upper(O_REGS+14),16'o31006,"stop after changed original instruction");
// A restoration fault keeps the armed record until cleanup really succeeds.
command("P 31000");command("R 7 30776");baseprompt=prompts;segment="";send({"C",8'd13});
wait(request && !bank && dut.engine.fetching && address==16'o31000);
@(negedge clk);fail_write=1;wait(prompts>baseprompt);contains("?BP");
eq(memory['o31000/2],0,"failed restoration still physically patched");
eq(upper(O_BPTAB+4)&2,2,"failed restore retains armed ownership");
oldfetches=guest_fetches;command("C");contains("?BP");eq(16'(guest_fetches),16'(oldfetches),"cannot resume over unresolved patch");
command("Z");contains("?BP");eq(upper(O_BPTAB+4)&2,2,"CLEAR cannot forget unresolved patch");
fail_write=0;command("Z");eq(memory['o31000/2],16'o005201,"retry restores original word");eq(upper(O_BPTAB+4),0,"ownership cleared after successful recovery");
// A failed TEMP restore cannot be forgotten by another U or T command.
command("R 7 30776");baseprompt=prompts;segment="";send({"U 31000",8'd13});
wait(request && !bank && dut.engine.fetching && address==16'o31000);
@(negedge clk);fail_write=1;wait(prompts>baseprompt);contains("?BP");
eq(upper(O_BPTEMP+4)&2,2,"failed temporary restoration retains ownership");
command("U 30004");contains("?BP");eq(upper(O_BPTEMP),16'o31000,"new RUN TO cannot replace unresolved temporary");
command("R 7 30000");command("T");contains("?BP");eq(upper(O_BPTEMP),16'o31000,"new STEP OVER cannot replace unresolved temporary");
fail_write=0;command("Z");eq(memory['o31000/2],16'o005201,"temporary recovery restores its original opcode");
// External short RESET cancels a free run and restores every live patch.
command("P 31000");command("R 7 20006");baseprompt=prompts;segment="";send({"U 30004",8'd13});
wait(!dut.engine.service_mode && memory['o31000/2]==0 && memory['o30004/2]==0);
repeat(1000)@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts>baseprompt);
eq(memory['o31000/2],16'o005201,"short RESET restores persistent patch");
eq(memory['o30004/2],16'o240,"short RESET restores temporary patch");
eq(upper(O_BPTEMP+4),0,"short RESET cancels RUN TO");eq(upper(O_BPTAB+4),1,"short RESET retains persistent definition");command("Z");
// A user point at the return site takes precedence over STEP OVER's point.
command("P 30004");command("R 7 30000");command("T");contains("BREAKPOINT");
eq(upper(O_REGS+14),16'o30004,"duplicate persistent/temporary target stops once");
eq(memory['o30004/2],16'o240,"duplicate ownership restores original once");command("Z");
command("P 20006");command("R 7 20006");oldfetches=guest_fetches;command("C");contains("BREAKPOINT");
eq(16'(guest_fetches-oldfetches),1,"self-loop at current point executes exactly once");command("Z");
// Successful loader copy also goes through the immutable resident gateway.
memory['o35000/2]=16'o65432;
command("P 20000");command("R 1 6000");command("R 2 1");command("R 5 35000");
command("R 4 125061");command("R 7 30200");baseprompt=prompts;oldfetches=guest_fetches;send({"C",8'd13});
wait(guest_fetches>oldfetches+4);
$display("COPY result RAM=%o R0=%o R1=%o R2=%o R4=%o R5=%o",upper('o6000),dut.engine.dp.rf.words[0],dut.engine.dp.rf.words[1],dut.engine.dp.rf.words[2],dut.engine.dp.rf.words[4],dut.engine.dp.rf.words[5]);
@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts>baseprompt);
eq(upper(O_REGS),0,"resident copy returns success");eq(upper('o6000),16'o65432,"resident copied USER word into HALT RAM");
eq(memory['o20000/2],16'o5201,"successful loader call disarms persistent patch");command("Z");
// A native HALT without the loader signature is a normal monitor stop.
command("P 20000");command("R 4 1234");command("R 7 30200");command("C");contains("HALT");
eq(upper(O_REGS+14),16'o30202,"foreign HALT preserves native next PC");
eq(upper(O_REGS+8),16'o1234,"foreign HALT preserves R4");command("Z");
memory['o1000/2]=16'o240;memory['o157776/2]=16'o240;
command("P 1000");contains("BP 001000");command("P 157776");contains("BP 157776");command("Z");
// Even a dropped HALT-vector restoration is verified before guest execution.
command("P 20000");command("R 7 20006");baseprompt=prompts;send({"C",8'd13});
wait(!dut.engine.service_mode && memory['o20000/2]==0);drop_vector=1;
@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts>baseprompt);
contains("?BP");eq(memory['o20000/2],16'o5201,"vector restore failure still restores guest opcode");
oldfetches=guest_fetches;command("C");contains("?BP");eq(16'(guest_fetches),16'(oldfetches),"cannot resume with unresolved vector restoration");
drop_vector=0;command("Z");eq(upper('o170),16'o200,"retry verifies native HALT vector restoration");
eq(16'(io_bad),0,"breakpoint paths never read unknown I/O registers");
$display("PASS CP66: %0d checks %0d clocks",checks,cycles);$finish;
