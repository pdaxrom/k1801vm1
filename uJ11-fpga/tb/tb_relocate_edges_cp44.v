`timescale 1ns/1ps
// Actual CP44 board/CPU/FRAM path. Only bootstrap overlay is disabled so the
// directed program is fetched from FRAM; no CPU register or APR data is forced.
module tb_relocate_edges_cp44;
    reg clk=0,reset=1;
    wire stopped,fc,fs,fm,fi;
    integer cursor=0,expected_count=0,checks=0,clocks=0,csr_beats=0,lookups=0;
    integer i,phase=0,finish_pc,high_fetches=0,frame_writes=0;
    reg [15:0] par1;reg is_fault;
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
            if(dut.acknowledge && dut.opcode_fetch && dut.address>=22'h10000 && dut.address<22'h20000)high_fetches=high_fetches+1;
            if(dut.acknowledge && dut.writing && (dut.address==22'h101fc || dut.address==22'h101fe))frame_writes=frame_writes+1;
            if((phase==6 || phase==7) && dut.bus_request && dut.virtual_address==16'o20001)
                $fatal(1,"odd word reached translated external bus");
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
        for(phase=1;phase<=7;phase=phase+1)begin
            reset=1;cursor=0;checks=0;expected_count=0;frame_writes=0;
            memory.memory[17'o40774]=8'ha5;memory.memory[17'o40775]=8'h5a;
            memory.memory[65536]=8'hef;memory.memory[65537]=8'hbe;
            emit(16'o000137);emit(16'o2000);emit(16'o1000);emit(0);
            cursor=512;emit(1);cursor=1024;
            store(0,16'o172340,0);store(0,16'o172344,16'h0400);
            store(0,16'o172356,16'hff80);
            par1=phase<=2 ? 16'h2400 : phase<=4 ? 16'h0f80 : 16'h0400;
            store(0,16'o172342,par1);
            store(0,16'o172516,(phase==2 || phase==4) ? 0 : 16'o20);
            emit(16'o012706);emit(16'o41000); // Page2 maps the trap stack high.
            store(0,16'o177572,1);
            is_fault=phase==1 || phase==3 || phase==6 || phase==7;
            if(phase==1 || phase==3)begin emit(16'o013701);emit(16'o20000);end
            else if(phase==2)load_check(0,16'o20000,16'hbeef);
            else if(phase==4)load_check(0,16'o032516,0); // 18-bit I/O canonicalization.
            else if(phase==5)begin
                store(1,16'o20001,16'h0080);load_check(1,16'o20001,16'hff80);
                load_check(0,16'o20000,16'h80ef);
            end else if(phase==6)begin emit(16'o013701);emit(16'o20001);end
            else store(0,16'o20001,16'h1234);
            emit(1);finish_pc=cursor;
            start();wait(dut.cpu.waiting);@(negedge clk);
            if(dut.cpu.engine.dp.rf.words[7] !== (is_fault ? 16'o1002 : 16'(finish_pc)) || checks!=expected_count)
                $fatal(1,"CPU edge phase%0d PC%o checks%0d expected%0d",phase,dut.cpu.engine.dp.rf.words[7],checks,expected_count);
            if(is_fault && (frame_writes!=2 || dut.cpu.engine.dp.rf.words[6]!=16'o40774))
                $fatal(1,"trap frame not mapped to high FRAM phase%0d writes%0d SP%o",phase,frame_writes,dut.cpu.engine.dp.rf.words[6]);
            if({memory.memory[17'o40775],memory.memory[17'o40774]}!==16'h5aa5)
                $fatal(1,"mapped stack aliased low FRAM phase%0d",phase);
            if(phase!=5 && {memory.memory[65537],memory.memory[65536]}!==16'hbeef)
                $fatal(1,"fault/alias corrupted high RAM");
        end
        $display("PASS CP44 CPU edges: 18-bit wrap, 22-bit NXM, canonical I/O, MOVB sign/lane, odd read/write vector4 and high mapped trap stack");
        // Change page0 so the first opcode after MMR0 enable comes from bank1.
        phase=8;reset=1;cursor=0;checks=0;expected_count=0;high_fetches=0;
        emit(16'o000137);emit(16'o2000);cursor=1024;
        store(0,16'o172340,16'h0400);store(0,16'o177572,1);
        emit(16'o012700);i=cursor;emit(16'h1111);emit(1);finish_pc=cursor;
        for(cursor=0;cursor<finish_pc;cursor=cursor+1)memory.memory[65536+cursor]=memory.memory[cursor];
        memory.memory[65536+i]=8'hef;memory.memory[65537+i]=8'hbe;
        start();wait(dut.cpu.waiting);@(negedge clk);
        if(dut.cpu.engine.dp.rf.words[0]!==16'hbeef || dut.cpu.engine.dp.rf.words[7]!==16'(finish_pc) || high_fetches!=2)
            $fatal(1,"mapped opcode/immediate stream R0%h PC%o highfetch%0d",dut.cpu.engine.dp.rf.words[0],dut.cpu.engine.dp.rf.words[7],high_fetches);
        $display("PASS CP44 mapped instruction stream: high-bank opcode/immediate fetch after MMR0 enable");
        $finish;
    end
endmodule
