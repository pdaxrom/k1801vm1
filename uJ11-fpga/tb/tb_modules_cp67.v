`timescale 1ns/1ps
module tb_modules_cp67;
    reg clk=0,reset=1,uart_rx=1;
    wire fc,fs,fm,fi,stopped;
    integer clocks=0,scenario=0,checks=0,i;
    reg [15:0] expected_pc=16'o4000;
    reg fail_read=0;
    uj11_board dut(.clk(clk),.reset(reset),.halt_button(1'b0),.uart_rx(uart_rx),.uart_tx(),
        .panel_keys(4'b0),.panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #17 clk=~clk;
    always @(negedge clk) begin
        if(fail_read && dut.bus_request && dut.bank && !dut.writing && dut.address==16'o7500)
            force dut.error=1'b1;
        else release dut.error;
    end
    always @(posedge clk) if(!reset) begin
        clocks<=clocks+1;
        if(clocks>100000000 || stopped)
            $fatal(1,"CP67 case%0d timeout/stopped PC%o IR%o uPC%h",scenario,dut.cpu.engine.dp.rf.words[7],dut.cpu.ir,dut.cpu.debug_upc);
    end
    task put(input integer address,input [15:0] value);
        begin fram.memory[65536+address]=value[7:0];fram.memory[65537+address]=value[15:8];end
    endtask
    function [15:0] peek(input integer address);
        peek={fram.memory[65537+address],fram.memory[65536+address]};
    endfunction
    task eq(input [15:0] got,want,input string what);
        begin
            checks=checks+1;
            if(got!==want)$fatal(1,"CP67 case%0d %s got%o want%o",scenario,what,got,want);
        end
    endtask
    task fresh;
        begin
            @(negedge clk);reset=1;fail_read=0;uart_rx=1;expected_pc=16'o4000;repeat(4)@(negedge clk);
            for(i=0;i<131072;i=i+1)fram.memory[i]=0;
            clocks=0;
        end
    endtask
    task boot;
        begin
            @(negedge clk);reset=0;wait(!dut.cpu.engine.service_mode);@(negedge clk);
            eq(dut.cpu.engine.dp.rf.words[7],expected_pc,"selected bootstrap entry");
            eq(dut.cpu.engine.dp.rf.words[6],16'o1000,"cold stack balanced");
            eq(dut.cpu.psw,0,"initial USER PSW");
            eq(peek(4),16'o312,"resident fault vector restored");
            eq(dut.bus.boot_overlay_active,0,"HALT ROM released before module entry");
            eq(dut.bus.boot_complete,0,"module initialization does not fake SD completion");
            $display("CP67 case%0d cold clocks %0d",scenario,clocks);
        end
    endtask
    task send_byte(input [7:0] value);
        integer bitno;
        begin
            uart_rx=0;repeat(256)@(negedge clk);
            for(bitno=0;bitno<8;bitno=bitno+1)begin uart_rx=value[bitno];repeat(256)@(negedge clk);end
            uart_rx=1;repeat(256)@(negedge clk);
        end
    endtask
    initial begin
        #100;
        `include "module_cases.vh"
        $display("PASS CP67 modules: %0d cases, %0d checks, actual SPI FRAM",scenario,checks);
        $finish;
    end
endmodule
