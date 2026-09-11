// Inserted after the established tb_board_bus scenario by run_fram_cp52.py.
// The original fixture still owns reset, UART/SD/RK setup and the bus tasks.
        memory.memory[16'o003774]=8'h12;memory.memory[16'o003775]=8'h34;
        memory.memory[16'o003776]=8'h56;memory.memory[16'o003777]=8'h78;
        beat(0,3,16'o003774,0,1);
        if(answer!==16'h3412 || fc)$fatal(1,"RAM read did not retain");
        previous_edges=memory.transaction_count;
        beat(0,3,16'o003776,0,0);
        if(answer!==16'h7856 || fc || memory.transaction_count!=previous_edges)$fatal(1,"sequential extension word");
        beat(0,3,16'o004000,0,1);
        if(answer!==firmware[0] || !fc || memory.transaction_count!=previous_edges)$fatal(1,"RAM to boot ROM boundary");
        // Boot vector overlays at 024/026 are discontinuities too.
        memory.memory[16'o000022]=8'hbc;memory.memory[16'o000023]=8'h9a;
        beat(0,3,16'o000022,0,0);if(answer!==16'h9abc || fc)$fatal(1,"pre-vector RAM");
        previous_edges=memory.transaction_count;
        beat(0,3,16'o000024,0,0);
        if(!fc || memory.transaction_count!=previous_edges)$fatal(1,"vector close clocked FRAM");
        // Aligned board byte reads still return a full word; a high byte must
        // not change the transport cursor by one, nor be cached for later.
        beat(0,2,16'o003775,0,0);if(answer!==16'h3412 || fc)$fatal(1,"aligned high byte");
        previous_edges=memory.transaction_count;
        beat(0,1,16'o003776,0,0);
        if(answer!==16'h7856 || memory.transaction_count!=previous_edges)$fatal(1,"word cursor after byte read");
        beat(0,3,16'o177750,0,0);
        if(answer!=16'o31 || !fc || memory.transaction_count!=previous_edges)$fatal(1,"CSR close");
        // A private RK WRITE copy can read the I/O page as physical FRAM;
        // it must not leave a read open across service-ROM/CSR transitions.
        beat(1,3,16'o177440,16'o2023,0);accept_irq();
        if(!dut.rk_service_active || !dut.rk_write_command)$fatal(1,"RK WRITE fixture entry");
        beat(0,3,movb_address[15:0],0,1);
        memory.memory[16'o177566]=8'he1;memory.memory[16'o177567]=8'hd2;
        previous_edges=memory.transaction_count;
        beat(0,1,16'o177566,0,0);
        if(answer!==16'hd2e1 || !fc || memory.transaction_count!=previous_edges+1)
            $fatal(1,"RK physical read classification");
        beat(0,3,16'o160476,0,1);
        if(dut.rk_service_active || !fc)$fatal(1,"RK overlay release");
        beat(0,3,16'o003774,0,1);if(fc)$fatal(1,"reset fixture not parked");
        peripheral_reset=1;repeat(3)@(negedge clk);peripheral_reset=0;
        if(!fc || fs)$fatal(1,"peripheral reset retained CS");
        $display("PASS CP52 board: sequential opcode/extension/byte, boot/vector/CSR boundaries, RK physical read and overlay release, peripheral reset");
