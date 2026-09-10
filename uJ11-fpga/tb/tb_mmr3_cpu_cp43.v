`timescale 1ns/1ps
// Actual CP43 board/CPU/FRAM path. Only bootstrap overlay is disabled so the
// directed program is fetched from FRAM; no CPU register or APR data is forced.
module tb_mmr3_cpu_cp43;
    reg clk=0,reset=1;
    wire stopped,fc,fs,fm,fi;
    integer cursor=0,expected_count=0,checks=0,clocks=0,csr_beats=0,lookups=0;
    integer i,phase=0;
    reg [15:0] expected[0:1023];
    always #5 clk=~clk;
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(1'b1),.uart_tx(),
        .panel_keys(4'hf),.panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),
        .panel_blank(),.panel_latch(),.host_miso(),.host_miso_oe(),
        .fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.stopped(stopped));
    spi_fram_model memory(fc,fs,fm,fi);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task emit(input [15:0] word_value);
        begin
            memory.memory[cursor]=word_value[7:0];memory.memory[cursor+1]=word_value[15:8];
            cursor=cursor+2;
        end
    endtask
    task store(input bit byte_op,input [15:0] addr,value);
        begin emit(byte_op ? 16'o112737 : 16'o012737);emit(value);emit(addr);end
    endtask
    task load_check(input bit byte_op,input [15:0] addr,value);
        begin
            emit(byte_op ? 16'o113700 : 16'o013700);emit(addr);
            expected[expected_count]=value;expected_count=expected_count+1;
        end
    endtask
    task start;
        begin
            repeat(5)@(negedge clk);
            dut.bus.boot_overlay_active=0;dut.bus.boot_release_armed=0;
            reset=0;
        end
    endtask
    always @(posedge clk)begin
        if(!reset)begin
            clocks=clocks+1;
            if(clocks>2000000)$fatal(1,"CPU CSR timeout phase%0d PC%o",phase,dut.cpu.engine.dp.rf.words[7]);
            if(dut.bus.mmr3_ready && dut.request && dut.bus.mmr3_selected)csr_beats=csr_beats+1;
            if(dut.apr_grant)lookups=lookups+1;
            if(dut.apr_csr_request && dut.apr_request)$fatal(1,"CPU/lookup ownership collision");
            if(dut.bus.mmr3_selected && dut.request && (dut.bus.fram_request || dut.rom_enable || dut.bus.uart_strobe || dut.bus.sd_strobe))
                $fatal(1,"CSR leaked to peripheral");
        end
        #1;
        if(!reset && dut.cpu.retire &&
           (dut.cpu.ir==16'o013700 || dut.cpu.ir==16'o113700))begin
            if(checks>=expected_count || dut.cpu.engine.dp.rf.words[0]!==expected[checks])
                $fatal(1,"CPU CSR check%0d got%h expected%h",checks,dut.cpu.engine.dp.rf.words[0],expected[checks]);
            checks=checks+1;
        end
    end
    initial begin
        #1;
        emit(16'o000137);emit(16'o2000);cursor=1024;
        emit(16'o012706);emit(16'o1400);
        load_check(0,16'o172516,0);
        for(i=0;i<64;i=i+1)begin
            store(0,16'o172516,16'hffc0 | 16'(i));load_check(0,16'o172516,16'(i));
            load_check(1,16'o172516,16'(i));load_check(1,16'o172517,0);
            store(1,16'o172517,16'hffff);load_check(0,16'o172516,16'(i));
            store(1,16'o172516,16'hff00 | (16'd255 ^ 16'(i)));
            load_check(0,16'o172516,16'd63 ^ 16'(i));
        end
        store(0,16'o172516,16'o77);
        store(0,16'o172340,16'ha55a);load_check(0,16'o172516,16'o77);
        emit(16'o000005); // Kernel RESET must clear MMR3, preserving APR RAM.
        load_check(0,16'o172516,0);load_check(0,16'o172340,16'ha55a);
        emit(1);start();wait(dut.cpu.waiting);@(negedge clk);
        if(checks!=expected_count || checks!=324 || csr_beats!=516 || lookups==0)
            $fatal(1,"CPU MMR3 counts checks%0d expected%0d beats%0d",checks,expected_count,csr_beats);
        $display("PASS CPU MMR3: %0d readbacks, %0d beats, %0d lookup reads, %0d clocks; all values, MOV/MOVB, RESET, APR persistence",checks,csr_beats,lookups,clocks);
        // Odd read and write must fault before reaching the CSR; vector4
        // handler reads back the last valid value and then executes WAIT.
        for(phase=1;phase<3;phase=phase+1)begin
            reset=1;cursor=0;checks=0;expected_count=0;
            emit(16'o000137);emit(16'o2000);emit(16'o1000);emit(0);
            cursor=512;load_check(0,16'o172516,16'o53);emit(1);
            cursor=1024;emit(16'o012706);emit(16'o1400);
            store(0,16'o172516,16'o53);
            if(phase==1)store(0,16'o172517,16'h1234);
            else begin emit(16'o013701);emit(16'o172517);end
            emit(1);i=csr_beats;start();wait(dut.cpu.waiting);@(negedge clk);
            if(csr_beats!=i+2 || checks!=1 || dut.cpu.engine.dp.rf.words[7]!=16'o1006)
                $fatal(1,"odd CSR phase%0d beats%0d before%0d PC%o checks%0d",phase,csr_beats,i,dut.cpu.engine.dp.rf.words[7],checks);
        end
        $display("PASS CPU MMR3 odd: word read/write vector4, CSR preserved and no failed bus beat");
        $finish;
    end
endmodule
