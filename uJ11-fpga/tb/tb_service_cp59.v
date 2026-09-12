`timescale 1ns/1ps
// Execute installation and service firmware as PDP-11 instructions. No forcing
// bank, ready, CPC/CPSW or architectural registers from the testbench.
module tb_service_cp59 #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1,irq_valid=0;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,irq_ack,waiting,stopped;
    wire [1:0] fault;
    wire [9:0] upc;
    reg [15:0] memory[0:65535];
    integer delay_cycles=0,wait_count=0,cycles=0,beats=0,checks=0,irq_count=0;
    integer scenario=0,i;
    reg inject_error=0,inject_irq=0,irq_sent=0;
    reg [15:0] error_address=16'h1234, error_address2=16'hffff;
    reg error_bank=1;
    integer error_beats=0;
    wire ack=request && wait_count==delay_cycles;
    wire error=inject_error && dut.engine.service_mode && bank==error_bank &&
        (address==error_address || address==error_address2);
    wire [15:0] word_data=memory[{bank,address[15:1]}];
    assign rdata=byte_access ? {8'b0,address[0]?word_data[15:8]:word_data[7:0]} : word_data;
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(3'd7),.irq_vector(15'o70000),
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
        if(reset)begin wait_count<=0;irq_count<=0;end
        else begin
            cycles<=cycles+1;
            if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
            if(request && ack)begin
                beats<=beats+1;
                if(error)error_beats<=error_beats+1;
                if(writing && !error)begin
                    if(!byte_access)memory[{bank,address[15:1]}]<=wdata;
                    else if(address[0])memory[{bank,address[15:1]}][15:8]<=wdata[7:0];
                    else memory[{bank,address[15:1]}][7:0]<=wdata[7:0];
                end
            end
            if(irq_ack)begin
                if(dut.engine.service_mode)$fatal(1,"IRQ entered active service");
                irq_count<=irq_count+1;irq_valid<=0;
            end
            if(inject_irq && dut.engine.service_mode && !irq_sent)begin irq_valid<=1;irq_sent<=1;end
            if(cycles>100000)$fatal(1,"timeout scenario %0d PC%o IR%o uPC%h",scenario,dut.engine.dp.rf.words[7],ir,upc);
        end
    end
    task fresh;
        begin
            @(negedge clk);reset=1;irq_valid=0;inject_irq=0;inject_error=0;irq_sent=0;error_address=16'h1234;error_address2=16'hffff;error_bank=1;error_beats=0;
            repeat(4)@(negedge clk);
            for(i=0;i<65536;i=i+1)memory[i]=16'ha55a;
            cycles=0;beats=0;
        end
    endtask
    task go;
        begin @(negedge clk);reset=0;end
    endtask
    task finish_case;
        begin
            wait(waiting || stopped);@(negedge clk);
            $display("case %0d: %0d clocks %0d external beats mode%0d fault%0d",scenario,cycles,beats,dut.engine.service_mode,fault);
            checks=checks+1;
        end
    endtask
    task expect16(input [15:0] got,want,input string what);
        if(got!==want)$fatal(1,"case %0d %s got%o want%o PC%o IR%o uPC%h",scenario,what,got,want,dut.engine.dp.rf.words[7],ir,upc);
    endtask
    initial begin
        #100;
        `include "service_cp59_cases.vh"
        $display("PASS CP59 CPU: %0d scenarios; ROM_DECODE=%0d",checks,ROM_DECODE);
        $finish;
    end
endmodule
