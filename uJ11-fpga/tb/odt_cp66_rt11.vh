        // Continue uses actual HALT RAM gateway and real serial FRAM traffic.
        cmdtext=$sformatf("P %o",G_LOOP);odt_command(cmdtext);
        cmdtext=$sformatf("R 7 %o",G_LOOP+2);odt_command(cmdtext);
        odt_command("C");contains("BREAKPOINT");
        check(upper(O_REGS+14)==16'(G_LOOP),"real FRAM persistent breakpoint before INC");
        check({fram.memory[G_LOOP+1],fram.memory[G_LOOP]}==16'o5201,"real FRAM opcode restored at prompt");
        odt_command("Z");
        cmdtext=$sformatf("R 7 %o",G_WKCALL);odt_command(cmdtext);
        old_irq=irq_total;odt_command("T");contains("OVER RETURN");
        check(upper(O_REGS+14)==16'(G_WKDONE),"STEP OVER through native WAIT/KW11-L");
        check({fram.memory[G_TICKS+1],fram.memory[G_TICKS]}==1,"native timer handler executed once");
        check(irq_total>old_irq,"real board IRQ acknowledged while temporary point live");
        check(upper(O_REGS+12)=={fram.memory[G_OLDSP+1],fram.memory[G_OLDSP]},"timer STEP OVER balances stack");
        cmdtext=$sformatf("R 7 %o",G_DKCALL);odt_command(cmdtext);
        old_irq=irq_total;old_rk=rk_starts;odt_command("T");contains("OVER RETURN");
        check(upper(O_REGS+14)==16'(G_DKDONE),"STEP OVER through RT-11 LOOKUP/READW/CLOSE");
        check({fram.memory[G_DKOK+1],fram.memory[G_DKOK]}==1,"RT-11 read actual ODT.BIN header");
        check(rk_starts>old_rk,"real RK/SD service ran within STEP OVER");
        check(irq_total>old_irq,"normal IRQ during RT-11 file read");
        check(upper(O_REGS+12)=={fram.memory[G_OLDSP+1],fram.memory[G_OLDSP]},"disk STEP OVER balances stack");
        check(upper('o170)==16'o200,"resident HALT vector restored after disk read");
        // Return to the deliberately odd-SP/IPL7 main fixture as a pair.
        // The IRQ/disk subroutines intentionally changed IPL to zero.
        cmdtext=$sformatf("R 10 %o",oldpsw);odt_command(cmdtext);
