`timescale 1ns/1ps
// CPU contracts with executable HALT boot and a tiny memory-command monitor.
// Only memory contents and external pins are supplied; no CPU state forcing.
module tb_debug_cp63 #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1,halt_button=0,debug_block=0,irq_valid=0;
    reg [2:0] irq_priority=7;
    reg [14:0] irq_vector=15'o40;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,irq_ack,waiting,stopped;
    wire [9:0] upc;
    wire [1:0] fault;
    reg [15:0] memory[0:65535];
    integer delay_cycles=0,wait_count=0,cycles=0,checks=0,scenario=0,i;
    integer guest_fetches=0,irq_count=0,debug_count=0;
    reg inject_error=0;
    wire ack=request && wait_count==delay_cycles;
    wire error=inject_error && !bank && address==16'o30000;
    wire [15:0] word_data=memory[{bank,address[15:1]}];
    assign rdata=byte_access ? {8'b0,address[0]?word_data[15:8]:word_data[7:0]} : word_data;
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.halt_button(halt_button),.debug_block(debug_block),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),
        .irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),
        .mem_addr(address),.mem_write_data(wdata),.mem_request(request),.mem_read(),
        .mem_write(writing),.mem_byte(byte_access),.mem_bank(bank),.mem_physical(physical),
        .mem_ack(ack),.mem_error(error),.mem_read_data(rdata),.stopped(stopped),.fault_code(fault),
        .retire(),.debug_upc(upc),.debug_uword(),.ir(ir),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk) begin
        if(reset) begin wait_count<=0;irq_count<=0;end
        else begin
            cycles<=cycles+1;
            if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
            if(request && ack) begin
                if(!bank && dut.engine.fetching && !error)guest_fetches<=guest_fetches+1;
                if(writing && !error) begin
                    if(!byte_access)memory[{bank,address[15:1]}]<=wdata;
                    else if(address[0])memory[{bank,address[15:1]}][15:8]<=wdata[7:0];
                    else memory[{bank,address[15:1]}][7:0]<=wdata[7:0];
                end
            end
            if(irq_ack)begin irq_count<=irq_count+1;irq_valid<=0;end
            if(dut.engine.debug_ack)debug_count<=debug_count+1;
            if(cycles>100000)$fatal(1,"timeout case%0d PC%o IR%o uPC%h",scenario,dut.engine.dp.rf.words[7],ir,upc);
            if(stopped)$fatal(1,"terminal case%0d PC%o IR%o uPC%h fault%0d",scenario,dut.engine.dp.rf.words[7],ir,upc,fault);
        end
    end
    task fresh;
        begin
            @(negedge clk);reset=1;irq_valid=0;halt_button=0;debug_block=0;inject_error=0;
            repeat(4)@(negedge clk);
            for(i=0;i<65536;i=i+1)memory[i]=16'ha55a;
            cycles=0;guest_fetches=0;debug_count=0;
        end
    endtask
    task pulse;
        begin @(negedge clk);halt_button=1;@(negedge clk);halt_button=0;end
    endtask
    task eq(input integer got,want,input string what);
        begin
            if(got!==want)$fatal(1,"case%0d %s got%o want%o PC%o IR%o uPC%h",scenario,what,got,want,dut.engine.dp.rf.words[7],ir,upc);
            checks=checks+1;
        end
    endtask
    task monitor;
        begin
            wait(bank && request && dut.engine.fetching && address==MONITOR_POLL);
            @(negedge clk);
        end
    endtask
    task start_debug;
        begin
            @(negedge clk);reset=0;
            wait(dut.engine.debug_enabled);
            pulse();monitor();
        end
    endtask
    task command(input integer n);
        begin
            memory[(65536+COMMAND)/2]=16'(n);
            wait(!dut.engine.service_mode);
            if(n==2)monitor();
        end
    endtask
    `include "debug_constants.vh"
    initial begin
        #100;
        `include "debug_cases.vh"
        $display("PASS CP63 CPU: %0d cases %0d checks ROM_DECODE=%0d",scenario,checks,ROM_DECODE);$finish;
    end
endmodule
