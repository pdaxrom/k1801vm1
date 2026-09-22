// Runtime fixture, after payload initialization and before reset release.
memory['o143042/2]=16'o004567;memory['o143044/2]=16'o177324;
memory['o143046/2]=16'o000240;memory['o143050/2]=16'o012700;
memory['o143052/2]=16'o123456;memory['o143054/2]=16'o012767;
memory['o143056/2]=16'o123456;memory['o143060/2]=16'o000020;
for(integer i=0;i<40;i=i+1)memory['o40000/2+i]=16'o000240;
// JSR PC,sub1; INC R0; BR .  sub1 calls sub2, then returns.
memory['o30000/2]=16'o004767;memory['o30002/2]=16'o000014;
memory['o30004/2]=16'o005200;memory['o30006/2]=16'o000777;
memory['o30020/2]=16'o005201;memory['o30022/2]=16'o004767;
memory['o30024/2]=16'o000012;memory['o30026/2]=16'o005202;
memory['o30030/2]=16'o000207;memory['o30040/2]=16'o005203;
memory['o30042/2]=16'o000207;
memory['o30100/2]=16'o004767;memory['o30102/2]=16'o000014;
memory['o30120/2]=16'o000001;memory['o30122/2]=16'o005205;
memory['o30300/2]=16'o004767;memory['o30302/2]=16'o000014;
memory['o30320/2]=16'o000777;
// Recursive JSR at 30424 shares return PC 30430 at several stack depths.
memory['o30420/2]=16'o005305;memory['o30422/2]=16'o001403;
memory['o30424/2]=16'o004767;memory['o30426/2]=16'o177770;
memory['o30430/2]=16'o005203;memory['o30432/2]=16'o000207;

repeat(4)@(negedge clk);reset=0;wait(dut.engine.debug_enabled);
@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait(prompts==1);
command("A 0");contains("AUTO SCROLL OFF");

frames_before=display_frames;command("R");
for(integer r=0;r<8;r=r+1)contains($sformatf("R%0d=",r));contains("PSW=");
panel_check(display_frames==frames_before+1,"R dumps UART once but renders only selected R0");
scroll_window("R0=052525",0);eq(upper(O_RSEL),0,"panel starts register page at R0");
frames_before=display_frames;repeat(500000)@(negedge clk);
panel_check(display_frames==frames_before,"register page waits for a key");
for(integer r=1;r<9;r=r+1)begin
    segment="";keypress(18);eq(upper(O_RSEL),16'(r),"NEXT advances one register");
    if(r<8)contains($sformatf("R%0d=",r));else contains("PSW=");
end
keypress(18);eq(upper(O_RSEL),0,"register wrap to R0");
keypress(17);eq(upper(O_RSEL),8,"previous register wraps to PSW");
keypress(10);eq(upper(O_RSEL),0,"A shortcut selects R0");

command("D 143042");contains(photo_line);scroll_window(photo_line,0);
segment="";keypress(18);contains("[PANEL] N");contains("143046: 000240  NOP");eq(upper(O_DCUR),16'o143046,"NEXT skips extension word");
segment="";keypress(18);contains("143050: 012700 123456  MOV #123456,R0");
segment="";keypress(18);eq(upper(O_DCUR),16'o143054,"NEXT after immediate");
keypress(17);eq(upper(O_DCUR),16'o143050,"PREV is actual recorded boundary");
keypress(17);eq(upper(O_DCUR),16'o143046,"PREV across 2-byte instruction");
keypress(17);eq(upper(O_DCUR),16'o143042,"PREV across extension word");
command("L");contains("HISTORY START");
command("D 40000");for(integer i=0;i<35;i=i+1)command("N");
eq(upper(O_HCNT),32,"bounded 32-entry history");
for(integer i=0;i<31;i=i+1)command("L");
eq(upper(O_DCUR),16'o40010,"oldest retained instruction");
command("L");contains("HISTORY START");

command("D 143042");command("A 1");contains("AUTO SCROLL ON");
segment="";scroll_snapshot();auto_start=cycles;
for(integer round_trip=0;round_trip<2;round_trip=round_trip+1)begin
    for(integer offset=1;offset<=photo_line.len()-16;offset=offset+1)begin
        frames_before=display_frames;auto_at=cycles;
        while(display_frames==frames_before)@(negedge clk);
        @(negedge clk);auto_delta=cycles-auto_at;
        if(round_trip==0 && offset==1)auto_pause=auto_delta;
        if(round_trip==0 && offset==2)auto_step=auto_delta;
        scroll_window(photo_line,offset);scroll_unchanged();
    end
    for(integer offset=photo_line.len()-17;offset>=0;offset=offset-1)begin
        frames_before=display_frames;while(display_frames==frames_before)@(negedge clk);
        @(negedge clk);scroll_window(photo_line,offset);scroll_unchanged();
    end
end
panel_check(segment=="","automatic scrolling never prints UART");
panel_check(auto_pause>2*auto_step,"readable pause at an endpoint");
$display("AUTO scan timing: initial %0d clocks, interior %0d clocks, two trips %0d clocks",auto_pause,auto_step,cycles-auto_start);
command(">");command(">");command("<");eq(upper(O_SCDIR),16'hffff,"manual left selects reverse direction");
baseprompt=prompts;send({8'd27});wait(prompts>baseprompt);
scroll_window(photo_line,0);eq(upper(O_SCDIR),1,"ESC re-arms forward motion at the beginning");
frames_before=display_frames;while(display_frames==frames_before)@(negedge clk);
@(negedge clk);scroll_window(photo_line,1);scroll_unchanged();
command("A 0");frames_before=display_frames;repeat(1000000)@(negedge clk);
panel_check(display_frames==frames_before,"A 0 stops animation");

keypress(15);eq(upper(O_RADIX),16,"F toggles to HEX");
keypress(11);eq(upper(O_PANEDI),3,"B starts memory address editor");
oldfetches=guest_fetches;keypress(8);keypress(9);keypress(15);keypress(10);
eq(upper(O_PVAL),16'h89fa,"8-F remain digits in HEX numeric input");
eq(16'(guest_fetches),16'(oldfetches),"numeric 8/9 cannot execute steps");
baseprompt=prompts;send({8'd27});wait(prompts>baseprompt);
scroll_window("R0=5555",0);command("O");
keypress(12);eq(upper(O_PANEDI),4,"C starts disassembly address input");
keypress(1);keypress(4);keypress(3);keypress(0);keypress(4);keypress(2);keypress(19);
eq(upper(O_DCUR),16'o143042,"C accepts an octal address and shows disassembly");

command("R 6 150000");command("R 0 0");command("R 1 0");command("R 2 0");command("R 3 0");
command("R 7 30000");oldfetches=guest_fetches;command("T");contains("OVER RETURN");
eq(upper(O_REGS+14),16'o30004,"STEP OVER returns after complete JSR");
eq(upper(O_REGS+12),16'o150000,"STEP OVER restored call stack depth");
eq(upper(O_REGS),0,"does not execute return-site instruction");
eq(upper(O_REGS+2),1,"sub1 executed");eq(upper(O_REGS+4),1,"sub1 after nested call executed");eq(upper(O_REGS+6),1,"nested sub2 executed");
eq(16'(guest_fetches-oldfetches),7,"nested STEP OVER exact guest instructions");
command("T");eq(upper(O_REGS),1,"T on a non-JSR performs one step");
eq(upper(O_REGS+14),16'o30006,"non-call STEP OVER PC");
command("R 7 30424");command("R 3 0");command("R 5 3");
oldfetches=guest_fetches;command("T");contains("OVER RETURN");
eq(upper(O_REGS+14),16'o30430,"recursive STEP OVER return PC");
eq(upper(O_REGS+12),16'o150000,"recursive return must match original SP");
eq(upper(O_REGS+6),2,"inner returns sharing the target PC must not stop early");
eq(16'(guest_fetches-oldfetches),14,"recursive STEP OVER exact instructions");
command("R 7 30000");command("T 3");contains("OVER STEP LIMIT");
eq(upper(O_REGS+14),16'o30040,"budget stops at nested callee entry");
command("R 6 150000");command("R 7 30100");command("T");contains("OVER STOP: WAIT");
eq(upper(O_REGS+14),16'o30122,"WAIT preserves next PC");panel_check((upper(O_META)&16'o40)!=0,"WAIT context retained");
command("R 6 150000");command("R 7 30300");
over_prompt=prompts;send({"T",8'd13});
while(upper(O_OVACT)==0)@(negedge clk);
repeat(500000)@(negedge clk);send({8'd27});wait(prompts>over_prompt);
contains("OVER CANCELLED");eq(upper(O_OVACT),0,"UART aborts non-returning STEP OVER");
// Drain the queued ESC before the next command if cancellation printed a prompt.
repeat(300000)@(negedge clk);

command("R 6 150000");command("R 0 0");command("R 7 30000");
over_prompt=prompts;oldfetches=guest_fetches;
@(negedge clk);keycode=9;
while(upper(O_KLAST)!=9)@(negedge clk);
wait(prompts>over_prompt);repeat(1000000)@(negedge clk);
eq(upper(O_REGS+14),16'o30004,"held 9 cannot repeat after STEP OVER return");
eq(upper(O_REGS),0,"held 9 cannot execute the return-site instruction");
keycode=-1;while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
over_prompt=prompts;@(negedge clk);keycode=8;
while(upper(O_KLAST)!=8)@(negedge clk);
wait(prompts>over_prompt);repeat(1000000)@(negedge clk);
eq(upper(O_REGS),1,"held 8 shortcut STEP IN executes once");
eq(upper(O_REGS+14),16'o30006,"STEP IN next PC");
keycode=-1;while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
command("L");eq(upper(O_DCUR),16'o30004,"PREV keeps known boundary across a step");

command("R 6 150000");command("R 7 30300");
over_prompt=prompts;send({"T",8'd13});
while(upper(O_OVACT)==0)@(negedge clk);repeat(500000)@(negedge clk);
@(negedge clk);keycode=16;wait(prompts>over_prompt);contains("OVER CANCELLED");
eq(upper(O_OVACT),0,"panel aborts a non-returning call");
repeat(500000)@(negedge clk);panel_check(segment.len()>0,"cancellation result retained");
keycode=-1;while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
// E continues the stopped loop without changing its PC; return with HALT.
segment="";oldfetches=guest_fetches;over_prompt=prompts;
@(negedge clk);keycode=14;while(upper(O_KLAST)!=14)@(negedge clk);
repeat(10000)@(negedge clk);keycode=-1;wait(!dut.engine.service_mode);
repeat(1000)@(negedge clk);halt_button=1;@(negedge clk);halt_button=0;
wait(prompts>over_prompt);contains("[PANEL] C");
panel_check(guest_fetches>oldfetches,"E continues guest execution");
while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
// D opens GO address input, while ENTER uses the common checked G command.
keypress(13);eq(upper(O_PANEDI),2,"D starts GO address input");
keypress(3);keypress(0);keypress(0);keypress(0);keypress(6);
over_prompt=prompts;segment="";@(negedge clk);keycode=19;
while(upper(O_KLAST)!=19)@(negedge clk);repeat(10000)@(negedge clk);keycode=-1;
wait(!dut.engine.service_mode);repeat(1000)@(negedge clk);
halt_button=1;@(negedge clk);halt_button=0;wait(prompts>over_prompt);
contains("[PANEL] G 030006");eq(upper(O_REGS+14),16'o30006,"D/GO selects requested guest PC");
$display("PASS CP65: %0d checks %0d windows %0d clocks",checks,scroll_windows,cycles);$finish;
