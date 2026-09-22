`timescale 1ns/1ps
// The unchanged CP67 CPU executes an assembled PDP-11 module. Only external
// memory fixtures are initialized here; no RF/service/FPS RTL state is forced.
module tb_fpp_harness #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1,halt_button=0,irq_valid=0;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,irq_ack,waiting,stopped;
    wire [1:0] fault;
    wire [9:0] upc;
    reg [15:0] memory[0:65535];
    integer cycles=0,beats=0,checks=0,scenario=0,wait_count=0,delay_cycles=0;
    reg inject_error=0,error_bank=0,allow_stopped=0;
    reg [15:0] error_address=0;
    wire ack=request && wait_count==delay_cycles;
    wire error=inject_error && bank==error_bank && address==error_address;
    assign rdata=memory[{bank,address[15:1]}];
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.halt_button(halt_button),.debug_block(1'b0),
        .irq_valid(irq_valid),.irq_priority(3'd7),.irq_vector(15'o100),.irq_ack(irq_ack),
        .waiting(waiting),.peripheral_reset(),.mem_addr(address),.mem_write_data(wdata),
        .mem_request(request),.mem_read(),.mem_write(writing),.mem_byte(byte_access),
        .mem_bank(bank),.mem_physical(physical),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(),
        .debug_upc(upc),.debug_uword(),.ir(ir),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk) if(reset) wait_count<=0; else begin
        cycles<=cycles+1;
        if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
        if(ack)begin
            beats<=beats+1;
            if(writing && !error)begin
                if(byte_access)$fatal(1,"Unexpected byte write");
                memory[{bank,address[15:1]}]<=wdata;
            end
        end
        if(irq_ack)begin
            if(dut.engine.service_mode)$fatal(1,"IRQ inside FP11 service");
            irq_valid<=0;
        end
        if(cycles>50000 || (stopped && !allow_stopped))
            $fatal(1,"case%0d stopped/timeout PC%o IR%o uPC%h",scenario,dut.engine.dp.rf.words[7],ir,upc);
    end
    task eq(input [15:0] got,want,input string what);
        begin
            checks=checks+1;
            if(got!==want)$fatal(1,"case%0d %s got%o want%o PC%o IR%o uPC%h",scenario,what,got,want,dut.engine.dp.rf.words[7],ir,upc);
        end
    endtask
    `include "fpp_base_symbols.vh"
    reg [15:0] values[0:22];
    integer vecfile,rc,stride=1,total=0,executed=0,start_clocks,start_beats,metrics;
    reg [4095:0] measured=0;
    string vectors,metric_path;
    task prepare;
        begin
            @(negedge clk);reset=1;irq_valid=0;halt_button=0;inject_error=0;allow_stopped=0;
            repeat(4)@(negedge clk);cycles=0;beats=0;
            memory[32768]=16'o2400;memory[32769]=16'o340;
            memory[32770]=16'o312;memory[32771]=16'o340;
            memory[32768+16'o6000/2]=values[1];
            memory[32768+16'o6002/2]=values[2] & ~16'o20;
            for(integer r=0;r<7;r++)memory[32768+16'o6100/2+r]=values[3+r];
            memory[16'o1000/2]=values[0];
            memory[16'o1002/2]=16'o777;
            // Enter a T-set test with architectural RTT suppression; START
            // itself checks T before fetching, so it cannot seed this state.
            if(values[2][4])begin
                memory[16'o1000/2]=16'o6; // RTT
                memory[16'o1002/2]=values[0];
                memory[16'o1004/2]=16'o777;
                memory[32768+16'o6100/2+6]=values[9]-4;
                memory[(values[9]-16'd4)>>1]=16'o1002;
                memory[(values[9]-16'd2)>>1]=values[2];
            end
            @(negedge clk);reset=0;
            wait(!dut.engine.service_mode);@(negedge clk);
            eq(memory[32768+16'o6004/2],0,"cold initializer result");
            eq(dut.engine.service_ready,3,"FP preserves ODT ready");
            eq(dut.engine.debug_enabled,1,"FP preserves debug enable");
            for(integer a=0;a<24;a++)begin
                eq(memory[32768+FP_ACS/2+a],0,"cold initializes every AC word");
                memory[32768+FP_ACS/2+a]=16'h8100+a;
            end
            start_clocks=cycles;start_beats=beats;
        end
    endtask
    task finish_fp;
        begin wait(dut.engine.service_mode);wait(!dut.engine.service_mode);@(negedge clk);end
    endtask
