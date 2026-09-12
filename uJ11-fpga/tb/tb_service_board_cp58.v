`timescale 1ns/1ps
// Warm full-board service tests. Only the existing reset ROM overlay is
// suppressed; CPU state and service installation are established by instructions.
module tb_service_board_cp58;
    reg clk=0,reset=1;
    wire fc,fs,fm,fi,stopped;
    integer cycles=0,checks=0,scenario=0,i,delay_cycles=0;
    integer entry_start=-1,return_start=-1,entry_clocks=-1,return_clocks=-1;
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(1'b1),.uart_tx(),.panel_keys(4'b0),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #17 clk=~clk;
    always @(posedge clk)if(!reset)begin
        cycles<=cycles+1;
        if(dut.cpu.engine.step && dut.cpu.engine.service_enter)entry_start=cycles;
        if(dut.cpu.engine.step && dut.cpu.debug_upc==dut.cpu.engine.S_START)return_start=cycles;
        if(dut.bus_request && dut.opcode_fetch)begin
            if(dut.bank && entry_start>=0)begin entry_clocks=cycles-entry_start;entry_start=-1;end
            if(!dut.bank && return_start>=0)begin return_clocks=cycles-return_start;return_start=-1;end
        end
        if(cycles>200000)$fatal(1,"board timeout case%0d PC%o uPC%h",scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.debug_upc);
    end
    task put(input integer address,input [15:0] value);
        begin fram.memory[address]=value[7:0];fram.memory[address+1]=value[15:8];end
    endtask
    function [15:0] peek(input integer address);
        peek={fram.memory[address+1],fram.memory[address]};
    endfunction
    task fresh;
        begin
            @(negedge clk);reset=1;repeat(4)@(negedge clk);
            for(i=0;i<131072;i=i+2)put(i,16'ha55a);
            cycles=0;entry_start=-1;return_start=-1;entry_clocks=-1;return_clocks=-1;
        end
    endtask
    task go;
        begin @(negedge clk);reset=0;end
    endtask
    task finish_case;
        begin
            wait(dut.cpu.waiting || stopped);@(negedge clk);
            $display("board case%0d %0d clocks; entry%0d return%0d",scenario,cycles,entry_clocks,return_clocks);checks=checks+1;
        end
    endtask
    task expect16(input [15:0] got,want,input string what);
        if(got!==want)$fatal(1,"board case%0d %s got%o want%o PC%o",scenario,what,got,want,dut.cpu.engine.dp.rf.words[7]);
    endtask
    initial begin
        #100;
        force dut.bus.boot_overlay_active=1'b0;
        `include "service_cp58_board_cases.vh"
        $display("PASS CP58 full board: %0d service scenarios, actual SPI FRAM and I/O",checks);
        $finish;
    end
endmodule
