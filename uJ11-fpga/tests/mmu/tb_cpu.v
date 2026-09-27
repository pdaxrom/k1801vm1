`timescale 1ns/1ps
module tb_cpu;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0;always #5 clk=~clk;
    reg reset=1,halt_button=0,irq_valid=0;
    reg [2:0] irq_priority=0;reg [15:0] irq_vector=0;
    wire irq_ack,peripheral_reset,mem_request,mem_write,mem_byte,console_active,waiting,retire;
    wire [21:0] mem_address;wire [15:0] mem_write_data;
    reg mem_ready=0,mem_error=0;reg [15:0] mem_read_data=0;
    wire [15:0] psw,ir,pc,mmr0,mmr1,mmr2,mmr3,debug_register_data;
    wire [11:0] upc;wire [53:0] uword;
    reg [4:0] debug_register_address=0;
    uj11_mmu_cpu dut(.*);
    reg [15:0] ram[0:1048575];
    reg seen=0;integer delay_count=0,prompts=0,checks=0,cycles=0,i,pos,next_prompt,fp_error_pc;
    reg [7:0] rx=0;
    reg [71:0] uart_window=0;
    reg [15:0] saved0,saved1,saved2,saved_psw,saved_pc;
    always @(posedge clk)begin
        cycles<=cycles+1;mem_ready<=0;
        if(reset || !mem_request)begin seen<=0;delay_count<=0;end
        else if(!seen)begin
            if(delay_count==2)begin
                seen<=1;mem_ready<=1;mem_error<=0;
                if(mem_address<22'h200000)begin
                    mem_read_data<=ram[mem_address[20:1]];
                    if(mem_write)begin
                        if(!mem_byte || !mem_address[0])ram[mem_address[20:1]][7:0]<=mem_write_data[7:0];
                        if(!mem_byte || mem_address[0])ram[mem_address[20:1]][15:8]<=mem_write_data[15:8];
                    end
                end else case({mem_address[21:1],1'b0})
                    22'o17777560:mem_read_data<=rx!=0 ? 16'o200 : 0;
                    22'o17777562:begin mem_read_data<={8'b0,rx};if(!mem_write)rx<=0;end
                    22'o17777564:mem_read_data<=16'o200;
                    22'o17777566:if(mem_write)begin
                        $write("%c",mem_write_data[7:0]);
                        uart_window<={uart_window[63:0],mem_write_data[7:0]};
                        if(mem_write_data[7:0]==">" )prompts<=prompts+1;
                    end
                    default:begin mem_error<=1;mem_read_data<=0;end
                endcase
            end else delay_count<=delay_count+1;
        end
        if(cycles>150000)$fatal(1,"timeout PC=%o IR=%o uPC=%h PSW=%o MMR0=%o",pc,ir,upc,psw,mmr0);
    end
    task check(input bit ok,input [767:0] msg);
        begin checks=checks+1;if(!ok)$fatal(1,"%0s PC=%o uPC=%h PSW=%o",msg,pc,upc,psw);end
    endtask
    task wait_prompt(input integer n);
        begin while(prompts<n)@(negedge clk);repeat(3)@(negedge clk);end
    endtask
    task command(input string text);
        integer c,n;
        begin
            n=prompts+1;
            for(c=0;c<text.len();c=c+1)begin
                @(negedge clk);while(rx!=0)@(negedge clk);rx=text[c];
            end
            wait_prompt(n);
        end
    endtask
    task word(input [15:0] v);begin ram[pos/2]=v;pos=pos+2;end endtask
    task mov(input [15:0] value,input [15:0] address);
        begin word(16'o12737);word(value);word(address);end
    endtask
    task restart;
        begin @(negedge clk);reset=1;rx=0;repeat(4)@(negedge clk);reset=0;end
    endtask
    initial begin
        for(i=0;i<1048576;i=i+1)ram[i]=0;
        pos='o4000;
        word('o12700);word(1);word('o62700);word(2);word('o10037);word('o1000);
        word(0);word('o5200);word('o5200);word(0);
        restart();wait_prompt(1);
        check(debug_register_data==3 && ram['o1000/2]==3,"integer microcode executes");
        check(psw==16'o340,"ODT preserves PSW");
        rx="S";wait_prompt(2);
        check(debug_register_data==4,"STEP executes one instruction");
        rx="C";wait_prompt(3);
        check(debug_register_data==5,"CONTINUE returns to guest");
        // Kernel unified translation; keep code page zero and I/O page seven.
        pos='o4000;
        mov(16'o177406,16'o172300);mov(0,16'o172340);
        mov(16'o177406,16'o172302);mov(16'o10000,16'o172342);
        mov(16'o177406,16'o172316);mov(16'o177600,16'o172356);
        mov(16'o20,16'o172516);mov(1,16'o177572);
        mov(16'o123456,16'o20000);
        word(0);
        restart();wait_prompt(4);
        check(mmr0[0] && mmr3[4],"CPU enabled MMU via CSRs");
        check(ram[22'o1000000/2]==16'o123456,"CPU write above 64KiB");
        check(ram['o20000/2]==0,"no low RAM alias");
        // A protected write must enter vector 250 through kernel D mapping.
        pos='o4000;
        word('o12706);word('o2000);
        mov(16'o177406,16'o172300);mov(0,16'o172340);
        mov(16'o177402,16'o172302);mov(16'o10000,16'o172342);
        mov(16'o177406,16'o172316);mov(16'o177600,16'o172356);
        mov(16'o20,16'o172516);mov(1,16'o177572);
        word('o12700);word('o20000);
        word('o12720);word('o7777); // MOV #7777,(R0)+: delta before protected write
        word(0);
        ram['o250/2]='o6000;ram['o252/2]='o340;
        ram['o6000/2]='o12701;ram['o6002/2]='o1234;ram['o6004/2]=0;
        restart();wait_prompt(5);
        check(mmr0[15:13]==1,"CPU protection abort uses MMR0");
        check(mmr1[7:0]==23 && mmr1[15:8]==16,"MMR1 preserves explicit PC and destination delta");
        debug_register_address=1;#1;
        check(debug_register_data=='o1234,"vector 250 handler executed");
        check(ram[22'o1000000/2]==16'o123456,"faulting store did not modify target");
        // User I/D, private stack, BPT kernel entry and RTI back to user.
        pos='o4000;
        word('o12706);word('o2000);
        mov(16'o177406,16'o172300);mov(0,16'o172340);
        mov(16'o177406,16'o172316);mov(16'o177600,16'o172356);
        mov(16'o177406,16'o177600);mov(16'o10000,16'o177640);
        mov(16'o177406,16'o177620);mov(16'o20000,16'o177660);
        mov(16'o21,16'o172516);mov(1,16'o177572);
        mov(16'o140000,16'o1776);mov(16'o4000,16'o1774);
        word('o12706);word('o1774);word(2);
        pos='o1004000;
        word('o12706);word('o2000);
        word('o13704);word('o1000); // MOV @#1000,R4 uses UD, immediate is UI
        word('o172427);word('o40200); // LDF #1: operand is in user I
        word('o172037);word('o1004); // ADD @#1004: pointer in I, data in D
        word('o174037);word('o1100); // STF result to user D
        word(3); // BPT
        word('o12703);word('o1234);word('o104000); // EMT
        ram['o2001000/2]='o76543;ram['o1001000/2]='o11111;
        ram['o2001004/2]='o40400;ram['o2001006/2]=0;
        ram['o1001004/2]='o41000;ram['o1001006/2]=0;
        ram['o1001100/2]='o7777;
        ram['o14/2]='o6000;ram['o16/2]='o340;
        pos='o6000;
        word('o13701);word('o177776); // record kernel PSW with PM=user
        word('o6506); // MFPI R6 pushes previous-mode SP
        word('o12605); // MOV (SP)+,R5
        word('o5202);word(2); // INC R2; RTI
        ram['o30/2]='o7000;ram['o32/2]='o340;ram['o7000/2]=0;
        restart();wait_prompt(6);
        debug_register_address=1;#1;check(debug_register_data==16'o30340,"trap selects kernel and records previous user");
        debug_register_address=2;#1;check(debug_register_data==1,"kernel BPT handler executed");
        debug_register_address=3;#1;check(debug_register_data==16'o1234,"RTI resumes user instruction stream");
        debug_register_address=4;#1;check(debug_register_data==16'o76543,"user split I/D operand read");
        debug_register_address=5;#1;check(debug_register_data==16'o2000,"MFPI accesses previous SP");
        debug_register_address=19;#1;check(debug_register_data==16'o2000,"user SP preserved across traps");
        check(psw[15:14]==0 && psw[13:12]==3,"EMT entered kernel before HALT");
        check(ram['o2001100/2]=='o40500 && ram['o2001102/2]==0,"FPP user immediate uses I, operand/result use D");
        check(ram['o1001100/2]=='o7777,"FPP user data store does not alias I space");
        saved0=mmr0;saved1=mmr1;saved2=mmr2;saved_psw=psw;saved_pc=pc;
        command("R 3\015");check(uart_window=="001234\015\n>","ODT register octal display");
        command("W 5670\015");check(uart_window=="005670\015\n>","ODT register deposit readback");
        debug_register_address=3;#1;check(debug_register_data==16'o5670,"ODT modifies selected GPR only");
        command("P 2001000\015");check(uart_window=="076543\015\n>","ODT physical 22-bit read");
        command("M 7\015");command("V 1000\015");check(uart_window=="076543\015\n>","ODT user data-space translation");
        command("W 54321\015");check(ram['o2001000/2]==16'o54321,"ODT virtual deposit");
        command("P 10000000\015");check(uart_window[31:0]=="?\015\n>","ODT NXM reports error");
        command("P 1\015");check(uart_window[31:0]=="?\015\n>","ODT odd word reports error");
        command("R 8\015");check(uart_window[31:0]=="?\015\n>","ODT rejects nonoctal input");
        command("R 3\015");check(uart_window=="005670\015\n>","ODT recovers after invalid input");
        check(mmr0==saved0 && mmr1==saved1 && mmr2==saved2,"ODT leaves guest MMU diagnostics unchanged");
        check(psw==saved_psw && pc==saved_pc,"ODT preserves guest PC/PSW");
        // Microcoded FPP control/state, including memory operands and faults.
        pos='o4000;
        word('o12706);word('o2000);
        word('o170011);word('o170012);word('o170200); // SETD; SETL; STFPS R0
        word('o170127);word(16'hffff);word('o170201); // LDFPS #all; STFPS R1
        word('o170000);word('o13702);word('o177776); // CFCC; MOV @#PSW,R2
        word('o170001);word('o170002); // SETF; SETI
        word('o170237);word('o1000); // STFPS @#1000
        word('o12703);word('o1100);word('o170323); // STST (R3)+
        fp_error_pc=pos;word('o170003); // illegal FP with FID: return, no trap
        word('o170337);word('o1104);
        word('o170127);word(0);word('o170003); // now exception vector 244
        ram['o244/2]='o10000;ram['o246/2]='o340;ram['o10000/2]=0;
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        debug_register_address=0;#1;check(debug_register_data==16'o300,"microcoded SETD/SETL");
        debug_register_address=1;#1;check(debug_register_data==16'hcfef,"LDFPS/STFPS mask and memory operand");
        debug_register_address=2;#1;check(debug_register_data==16'o357,"CFCC changes PSW condition codes");
        debug_register_address=3;#1;check(debug_register_data==16'o1104,"STST postincrement length is four bytes");
        check(ram['o1000/2]==16'hcf2f,"SETF/SETI and STFPS memory destination");
        check(ram['o1100/2]==0 && ram['o1102/2]==0,"reset FEC/FEA and STST second word");
        check(ram['o1104/2]==2 && ram['o1106/2]==fp_error_pc,"illegal FP captures FEC/FEA with FID");
        check(pc==16'o10002,"FP exception entered vector 244");
        saved_psw=psw;saved_pc=pc;
        command("F 0\015");check(uart_window=="100000\015\n>","ODT reads FPS");
        command("F 1\015");check(uart_window=="000002\015\n>","ODT reads FEC");
        command("F 4\015");command("W 40200\015");
        command("F 4\015");check(uart_window=="040200\015\n>","ODT deposits AC0 high word");
        check(psw==saved_psw && pc==saved_pc,"FP console access preserves guest context");
        // Register-set changes select an independent physical R0-R5 bank.
        pos='o4000;word('o12700);word(1);mov('o4000,'o177776);
        word('o12700);word(2);mov(0,'o177776);word(0);
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        debug_register_address=0;#1;check(debug_register_data==1,"primary R0 preserved");
        debug_register_address=8;#1;check(debug_register_data==2,"alternate R0 preserved");
        // A vector selects the destination mode and its stack. RT-11 XM
        // BASIC installs a user-mode TRAP handler for its error routines;
        // forcing kernel mode here corrupts the monitor's stack on return.
        pos='o4000;word('o12706);word('o2000);
        mov('o140000,'o177776);word('o12706);word('o3000);
        fp_error_pc=pos+2;word('o104400);
        word('o12703);word('o1234);word('o104000);
        ram['o34/2]='o6000;ram['o36/2]='o140340;
        pos='o6000;word('o13701);word('o177776);word('o5202);word(2);
        ram['o30/2]='o7000;ram['o32/2]='o340;ram['o7000/2]=0;
        ram['o2774/2]='o7777;ram['o2776/2]='o7777;
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        debug_register_address=1;#1;check(debug_register_data=='o170340,"TRAP vector selects user mode and previous user");
        debug_register_address=2;#1;check(debug_register_data==1,"user-mode TRAP handler executed");
        debug_register_address=3;#1;check(debug_register_data=='o1234,"user TRAP RTI resumes caller");
        debug_register_address=19;#1;check(debug_register_data=='o3000,"TRAP frame is removed from user stack");
        check(ram['o2774/2]==fp_error_pc && ram['o2776/2]=='o140000,"TRAP frame uses vector-selected user stack");
        // A supervisor vector must select SSP while preserving USP and KSP.
        pos='o4000;word('o12706);word('o2000);
        mov('o40000,'o177776);word('o12706);word('o2600);
        mov('o140000,'o177776);word('o12706);word('o3000);
        fp_error_pc=pos+2;word('o104400);
        word('o12703);word('o1234);word('o104000);
        ram['o36/2]='o40340;
        ram['o2574/2]='o7777;ram['o2576/2]='o7777;
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        debug_register_address=1;#1;check(debug_register_data=='o70340,"TRAP vector selects supervisor and previous user");
        debug_register_address=3;#1;check(debug_register_data=='o1234,"supervisor TRAP RTI resumes user caller");
        debug_register_address=17;#1;check(debug_register_data=='o2600,"supervisor SP restored after RTI");
        debug_register_address=19;#1;check(debug_register_data=='o3000,"supervisor vector preserves user SP");
        check(ram['o2574/2]==fp_error_pc && ram['o2576/2]=='o140000,"TRAP frame uses vector-selected supervisor stack");
        // A console stop of WAIT must resume WAIT until an unmasked IRQ.
        pos='o4000;word('o12706);word('o2000);word('o230);word(1);word('o5200);word(0);
        ram['o100/2]='o6000;ram['o102/2]='o340;
        ram['o6000/2]='o5201;ram['o6002/2]=2;
        next_prompt=prompts+1;restart();wait(waiting);
        @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;wait_prompt(next_prompt);
        saved_pc=pc;rx="C";wait(!console_active);repeat(100)@(negedge clk);
        check(waiting && pc==saved_pc,"ODT CONTINUE restores WAIT");
        @(negedge clk);irq_priority=6;irq_vector='o100;irq_valid=1;
        wait(irq_ack);@(negedge clk);irq_valid=0;
        next_prompt=prompts+1;wait_prompt(next_prompt);
        debug_register_address=0;#1;check(debug_register_data==1,"execution resumes after WAIT IRQ");
        debug_register_address=1;#1;check(debug_register_data==1,"IRQ handler and RTI executed");
        // STST's second word faults across a page boundary. The first write
        // is visible, but FP auto-update R3 and FPS have not committed.
        pos='o4000;word('o12706);word('o2000);
        mov(16'o177406,16'o172300);mov(0,16'o172340);
        mov(16'o177402,16'o172302);mov(16'o10000,16'o172342);
        mov(16'o177406,16'o172316);mov(16'o177600,16'o172356);
        mov(16'o20,16'o172516);mov(1,16'o177572);
        word('o12703);word('o17776);word('o170323);word(0);
        ram['o17776/2]='o7777;ram['o1000000/2]='o12345;
        ram['o250/2]='o6000;ram['o252/2]='o340;ram['o6000/2]=0;
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        debug_register_address=3;#1;check(debug_register_data==16'o17776,"FP abort discards R3 postincrement");
        check(mmr0[15:13]==1 && mmr1==0,"FP abort metadata excludes deferred increment");
        check(ram['o17776/2]==0 && ram['o1000000/2]=='o12345,"cross-page FP store faults before protected word");
        // Restart a D-precision ADD after its second operand word hits a
        // page-length abort. AC/FPS and the deferred R0 increment must be
        // unchanged in the handler. Extending PDR and replaying MMR2 then
        // produces exactly 1+2, advancing R0 once by eight bytes.
        pos='o4000;word('o12706);word('o2000);
        mov(16'o177406,16'o172300);mov(0,16'o172340);
        mov(16'o6,16'o172302);mov(16'o10000,16'o172342);
        mov(16'o177406,16'o172316);mov(16'o177600,16'o172356);
        mov(16'o20,16'o172516);mov(1,16'o177572);
        word('o170127);word('o40200);word('o172437);word('o1000);
        word('o12700);word('o20076);fp_error_pc=pos;word('o172020);
        word('o174037);word('o1200);word('o170237);word('o1210);word(0);
        for(i=0;i<4;i=i+1)begin ram['o1000/2+i]=0;ram['o1000076/2+i]=0;end
        ram['o1000/2]='o40200;ram['o1000076/2]='o40400;
        ram['o250/2]='o6000;ram['o252/2]='o340;
        pos='o6000;
        word('o13737);word('o177572);word('o1300);
        word('o13737);word('o177574);word('o1302);
        word('o13737);word('o177576);word('o1304);
        word('o10037);word('o1310);
        word('o174037);word('o1320);word('o170237);word('o1330);
        mov(16'o177406,16'o172302);
        word('o13716);word('o177576); // MOV @#MMR2,(SP): replay faulting PC
        mov(1,16'o177572);word(2);
        next_prompt=prompts+1;restart();wait_prompt(next_prompt);
        check(ram['o1300/2][15:13]==2 && ram['o1302/2]==0 && ram['o1304/2]==fp_error_pc,"FP page-length abort records restart PC without deferred deltas");
        check(ram['o1310/2]=='o20076 && ram['o1320/2]=='o40200 && ram['o1330/2]=='o40200,"FP abort preserves source GPR, AC and FPS");
        check(ram['o1200/2]=='o40500 && ram['o1202/2]==0 && ram['o1204/2]==0 && ram['o1206/2]==0,"restarted D ADD returns exactly three");
        debug_register_address=0;#1;check(debug_register_data=='o20106,"FP restart commits postincrement exactly once");
        $display("\nPASS MMU CPU: %0d checks, %0d cycles",checks,cycles);$finish;
    end
endmodule
