`timescale 1ns/1ps
// Actual CP44 board/CPU/FRAM path. Only bootstrap overlay is disabled so the
// directed program is fetched from FRAM; no CPU register or APR data is forced.
module tb_relocate_cpu_cp44 #(parameter integer WORDS=4096);
    reg clk=0,reset=1;
    wire stopped,fc,fs,fm,fi;
    integer cursor=0,expected_count=0,checks=0,clocks=0,csr_beats=0,lookups=0;
    integer i,mode,page,phase=0,loop_address,fail_fixups=0,finish_pc;
    integer patches[0:63];
    reg [7:0] low_snapshot[0:65535];
    reg [21:0] held_address;reg held=0;
    reg [15:0] pattern;
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
            if(clocks>200000000)$fatal(1,"CPU CSR timeout phase%0d PC%o",phase,dut.cpu.engine.dp.rf.words[7]);
            if(dut.bus_request && dut.raw_ack && (dut.bus.mmr3_selected || dut.bus.mmr0_selected))csr_beats=csr_beats+1;
            if(held && (!dut.bus_request || dut.address!==held_address))$fatal(1,"PA changed before ACK: %o -> %o",held_address,dut.address);
            if(dut.bus_request && !dut.acknowledge)begin held<=1;held_address<=dut.address;end
            else held<=0;
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
    task immediate(input [2:0] r,input [15:0] value);
        begin emit(16'o012700 | {13'b0,r});emit(value);end
    endtask
    task expect_equal;
        begin
            emit(16'o001402); // BEQ skips the two-word failure JMP.
            emit(16'o000137);patches[fail_fixups]=cursor;fail_fixups=fail_fixups+1;emit(0);
        end
    endtask
    initial begin
        #1;
        for(i=0;i<65536;i=i+1)memory.memory[i]=i[7:0]^8'h5a;
        emit(16'o000137);emit(16'o2000);cursor=1024;
        store(0,16'o172340,0); // KIPAR0 keeps code in place.
        store(0,16'o172344,16'hff80); // KIPAR2 supplies an alternate I/O mapping.
        store(0,16'o172356,16'h0400); // KIPAR7 deliberately does not map I/O.
        store(0,16'o177572,1); // Accepted unmapped beat must not restart mapped.
        store(0,16'o057572,0); // VA page2 -> MMR0; disabling must preserve its PA.
        load_check(0,16'o177572,0);
        store(0,16'o172356,16'hff80);
        store(0,16'o177572,1);
        for(mode=0;mode<2;mode=mode+1)begin
            store(0,16'o172516,mode==0 ? 0 : 16'o20);
            for(page=0;page<8;page=page+1)begin
                store(0,16'o172342,16'h0400+16'(page*128));
                pattern=16'h1234+16'(mode*16384+page*4096);
                immediate(0,pattern);immediate(1,16'o20000);immediate(2,16'(WORDS));
                loop_address=cursor;
                emit(16'o010021);emit(16'o005200);emit(16'o077200 | 16'((cursor+2-loop_address)/2));
                immediate(0,pattern);immediate(1,16'o20000);immediate(2,16'(WORDS));
                loop_address=cursor;
                emit(16'o020021);expect_equal();emit(16'o005200);
                emit(16'o077200 | 16'((cursor+2-loop_address)/2));
            end
        end
        store(0,16'o177572,0);load_check(0,16'o177572,0);
        emit(1);finish_pc=cursor;
        for(i=0;i<fail_fixups;i=i+1)begin
            memory.memory[patches[i]]=8'(cursor);memory.memory[patches[i]+1]=8'(cursor>>8);
        end
        immediate(0,16'hbad0);emit(1);
        if(cursor>=8192)$fatal(1,"program overlaps VA page1");
        for(i=0;i<65536;i=i+1)low_snapshot[i]=memory.memory[i];
        start();wait(dut.cpu.waiting);@(negedge clk);
        if(dut.cpu.engine.dp.rf.words[7]!==16'(finish_pc) || checks!=2)
            $fatal(1,"mapped compare/transition failed PC%o R0%h checks%0d",dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.dp.rf.words[0],checks);
        for(i=0;i<65536;i=i+1)if(memory.memory[i]!==low_snapshot[i])$fatal(1,"low FRAM alias/corruption addr%o",i);
        for(page=0;page<8;page=page+1)for(i=0;i<WORDS;i=i+1)begin
            pattern=16'h1234+16'(16384+page*4096+i);
            if({memory.memory[65537+page*8192+i*2],memory.memory[65536+page*8192+i*2]}!==pattern)
                $fatal(1,"upper FRAM page%0d word%0d expected%h",page,i,pattern);
        end
        $display("PASS CP44 CPU relocation: %0d words x 8 pages x 2 address modes, upper FRAM write/read, low bank intact; %0d clocks %0d control beats %0d PAR reads",WORDS,clocks,csr_beats,lookups);
        $display("PASS CP44 enable lifetime: unmapped enable with non-I/O KIPAR7, mapped disable via alternate I/O, stable physical requests");
        $finish;
    end
endmodule
