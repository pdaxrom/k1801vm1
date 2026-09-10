`timescale 1ns/1ps
// Actual CP39 board/CPU/FRAM path. Only bootstrap overlay is disabled so the
// directed program is fetched from FRAM; no CPU register or APR data is forced.
module tb_mmu_apr_csr_cpu;
    reg clk=0,reset=1;
    wire stopped,fc,fs,fm,fi;
    integer cursor=0,expected_count=0,checks=0,clocks=0,csr_beats=0,lookups=0;
    integer mode,space,page,i,phase=0;
    reg [15:0] base,pdr,par;
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
            if(dut.apr_csr_request && dut.apr_csr_ack)csr_beats=csr_beats+1;
            if(dut.apr_grant)lookups=lookups+1;
            if(dut.apr_csr_request && dut.apr_request)$fatal(1,"CPU/lookup ownership collision");
            if(dut.apr_csr_request && (dut.bus.fram_request || dut.rom_enable || dut.bus.uart_strobe || dut.bus.sd_strobe))
                $fatal(1,"CSR leaked to peripheral");
        end
        #1;
        if(!reset && phase==0 && dut.cpu.retire &&
           (dut.cpu.ir==16'o013700 || dut.cpu.ir==16'o113700))begin
            if(checks>=expected_count || dut.cpu.engine.dp.rf.words[0]!==expected[checks])
                $fatal(1,"CPU CSR check%0d got%h expected%h",checks,dut.cpu.engine.dp.rf.words[0],expected[checks]);
            checks=checks+1;
        end
    end
    initial begin
        #1;
        // Jump past vector/stack scratch. The program crosses a VA page,
        // while programming nonzero PAR/PDRs for every architectural pair.
        emit(16'o000137);emit(16'o17000);cursor=7680;
        for(mode=0;mode<3;mode=mode+1)begin
            case(mode)0:base=16'o172300;1:base=16'o172200;default:base=16'o177600;endcase
            for(space=0;space<2;space=space+1)for(page=0;page<8;page=page+1)begin
                pdr=base+16'(space*16+page*2);par=pdr+16'o40;
                store(0,pdr,16'hffff);load_check(0,pdr,16'hff0e);
                store(0,par,16'ha55a);load_check(0,par,16'ha55a);
                store(1,pdr,16'h0008);load_check(0,pdr,16'hff08);
                store(1,pdr+1,16'h0034);load_check(0,pdr,16'h3408);
                store(1,par+1,16'h00cd);load_check(0,par,16'hcd5a);
                store(1,par,16'h0080);load_check(0,par,16'hcd80);
                load_check(1,par,16'hff80);load_check(1,par+1,16'hffcd);
                load_check(0,pdr,16'h3408);
            end
        end
        emit(1);start();wait(dut.cpu.waiting);@(negedge clk);
        if(checks!=expected_count || checks!=432 || csr_beats!=720 || lookups==0)
            $fatal(1,"CPU CSR counts checks%0d expected%0d beats%0d",checks,expected_count,csr_beats);
        $display("PASS CPU CSR: %0d readbacks, %0d beats, %0d lookup reads, %0d clocks; 48 pairs, MOV/MOVB, lanes/sign extension",checks,csr_beats,lookups,clocks);
        // Odd word never reaches the CSR. Vector4 runs WAIT in FRAM;
        // the failed write must leave the previously programmed PAR intact.
        phase=1;reset=1;cursor=0;
        emit(16'o000137);emit(16'o2000);emit(16'o1000);emit(0);
        cursor=512;emit(1);cursor=1024;emit(16'o012706);emit(16'o1400);
        store(0,16'o172341,16'h1234);emit(1);
        i=csr_beats;start();wait(dut.cpu.waiting);@(negedge clk);
        if(csr_beats!=i || dut.cpu.engine.dp.rf.words[7]!=16'o1002)
            $fatal(1,"odd CSR: beats%0d before%0d PC%o fault%0d IR%o",csr_beats,i,dut.cpu.engine.dp.rf.words[7],dut.cpu.fault_code,dut.cpu.ir);
        // Verify persistence/readback through the CPU after reset, not by peeking RAM.
        phase=0;reset=1;cursor=0;checks=0;expected_count=0;
        emit(16'o000137);emit(16'o2000);cursor=1024;
        load_check(0,16'o172340,16'hcd80);emit(1);start();wait(dut.cpu.waiting);@(negedge clk);
        if(checks!=1)$fatal(1,"post-reset CSR read missing");
        $display("PASS CPU CSR odd/reset: vector4, no write, APR preserved");
        $finish;
    end
endmodule
