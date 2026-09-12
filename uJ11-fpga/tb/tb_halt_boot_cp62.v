`timescale 1ns/1ps
// Real reset ROM installation and SPI FRAM. For directed cases only, replace
// USER SD bootstrap by a RAM test program after its first START transition.
module tb_halt_boot_cp62;
    reg clk=0,reset=1;
    wire fc,fs,fm,fi,stopped;
    integer clocks=0,checks=0,scenario=0,i,boot_reads=0,boot_writes=0;
    integer inject=0,injected=0,entry_clock=0,service_clocks=0,boot_clocks=0;
    reg in_guest=0;
    reg [7:0] original_lower[0:65535];
    reg [7:0] expected_upper[0:65535];
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(1'b1),.uart_tx(),.panel_keys(4'b0),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #17 clk=~clk;
    always @(negedge fs) fram.write_protect=(inject==1 ||
        (inject==4 && fram.phase==3 && !fram.address[16]) ||
        (inject==2 && in_guest && fram.phase==3 && fram.address[16] && fram.address[15:0]>=16'o10000));
    // Inject a bus error at an otherwise valid MFUS source, preserving real ACK.
    always @(negedge clk) begin
        if(inject==3 && in_guest && dut.cpu.engine.service_mode &&
           dut.bus_request && !dut.bank && !dut.writing && dut.address==16'o020000) begin
            force dut.error=1'b1;injected=1;
        end else release dut.error;
    end
    always @(posedge clk) if(!reset) begin
        clocks<=clocks+1;
        if(clocks>500000)$fatal(1,"CP62 timeout case%0d PC%o IR%o uPC%h bank%b",scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.ir,dut.cpu.debug_upc,dut.bank);
        if(stopped)$fatal(1,"CP62 stopped case%0d PC%o uPC%h fault%0d",scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.debug_upc,dut.cpu.fault_code);
        if(dut.acknowledge && !in_guest) begin
            if((!dut.bank && (dut.address<16'o4000 || dut.address>=16'o4652 || dut.opcode_fetch)) ||
               (dut.physical && (dut.writing ||
               (dut.address!=16'o100 && dut.address!=16'o102))))
                $fatal(1,"startup copy used USER/raw access at %o",dut.address);
            if(dut.writing)boot_writes=boot_writes+1;
            else begin
                if(boot_reads==0 && dut.address!==0)$fatal(1,"first access is not HALT vector PC");
                if(boot_reads==1 && dut.address!==2)$fatal(1,"second access is not HALT vector PSW");
                boot_reads=boot_reads+1;
            end
            if(dut.opcode_fetch && (dut.rdata==16'o40 || dut.rdata==16'o41))$fatal(1,"startup used UJRD/UJWR");
        end
        if(in_guest && dut.cpu.engine.step && dut.cpu.engine.service_enter)entry_clock=clocks;
        if(in_guest && dut.cpu.engine.step && dut.cpu.engine.service_leave)service_clocks=clocks-entry_clock;
    end
    task put(input integer address,input [15:0] value);
        begin fram.memory[address]=value[7:0];fram.memory[address+1]=value[15:8];end
    endtask
    task gold(input integer address,input [15:0] value);
        begin expected_upper[address]=value[7:0];expected_upper[address+1]=value[15:8];end
    endtask
    function [15:0] peek(input integer address);
        peek={fram.memory[address+1],fram.memory[address]};
    endfunction
    task eq(input [15:0] got,want,input string what);
        if(got!==want)$fatal(1,"CP62 case%0d %s got%o want%o",scenario,what,got,want);
    endtask
    task fresh;
        begin
            @(negedge clk);reset=1;in_guest=0;inject=0;injected=0;
            release dut.bus.boot_overlay_active;
            repeat(4)@(negedge clk);
            for(i=0;i<131072;i=i+1)fram.memory[i]=8'ha5;
            for(i=0;i<65536;i=i+1)expected_upper[i]=8'ha5;
            for(i=0;i<256;i=i+2)put(8192+i,16'(i^16'hb649));
            clocks=0;boot_reads=0;boot_writes=0;service_clocks=0;
        end
    endtask
    task boot;
        begin
            for(i=0;i<65536;i=i+1)original_lower[i]=fram.memory[i];
            @(negedge clk);reset=0;
            wait(!dut.cpu.engine.service_mode);
            boot_clocks=clocks;in_guest=1;
            if(boot_writes!=517)$fatal(1,"startup writes %0d wanted295+9+213",boot_writes);
            force dut.bus.boot_overlay_active=1'b0;
            for(i=0;i<65536;i=i+1)begin
                if(i>=2048 && i<2474)original_lower[i]=expected_upper[i];
                if(fram.memory[i]!==original_lower[i])$fatal(1,"startup USER byte%o differs from expected bootstrap",i);
                if(fram.memory[65536+i]!==expected_upper[i])$fatal(1,"startup upper byte%o got%h expected%h",i,fram.memory[65536+i],expected_upper[i]);
            end
            eq(dut.cpu.psw,0,"initial USER PSW");
            eq(dut.cpu.engine.service_ready,0,"modules remain disabled");
            eq(dut.cpu.engine.dp.rf.words[7],16'o4000,"original SD entry");
            eq(dut.cpu.engine.dp.rf.words[6],16'o1000,"private HALT stack restored");
        end
    endtask
    task finish_case;
        begin wait(dut.cpu.engine.waiting);@(negedge clk);checks=checks+1;end
    endtask
    task compare_memory;
        begin
            for(i=0;i<65536;i=i+1) begin
                if(fram.memory[i]!==original_lower[i])$fatal(1,"USER memory changed case%0d byte%o",scenario,i);
                if(fram.memory[65536+i]!==expected_upper[i])$fatal(1,"upper memory case%0d byte%o got%h expected%h",scenario,i,fram.memory[65536+i],expected_upper[i]);
            end
        end
    endtask
    initial begin
        #100;
        `include "halt_boot_cases.vh"
        $display("PASS CP62: %0d cold HALT / copy cases, actual SPI FRAM",checks);
        $finish;
    end
endmodule
