`timescale 1ns/1ps
// Real CP63 CPU executing the production monitor. Directed RAM/UART bus model.
module tb_odt #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1,halt_button=0;
    wire [15:0] address,wdata,ir,rdata;
    wire request,writing,byte_access,bank,physical,stopped;
    wire [9:0] upc;
    reg [15:0] memory[0:65535];
    reg [7:0] rxbyte=0;
    reg rxfull=0,rxerr=0,txready=1,inject=0,fail_write=0,drop_write=0;
    integer cycles=0,checks=0,prompts=0,baseprompt=0,guest_fetches=0,oldfetches=0,io_bad=0,logfile;
    reg [39:0] suffix=0;
    string uart="",segment="",log_path;
    wire io=request && !physical && address>=16'o160000;
    wire error=(inject || (fail_write && writing)) && !bank && address==16'o31000;
    wire ack=request;
    wire [15:0] word_data=io ? (address==16'o177560 ? {3'b0,rxerr,4'b0,rxfull,7'b0} :
                         address==16'o177562 ? {8'b0,rxbyte} : address==16'o177564 ? {8'b0,txready,7'b0} : address[15:1]==15'o73000 ? 16'h8000 : 0) : memory[{bank,address[15:1]}];
    assign rdata=byte_access ? {8'b0,address[0]?word_data[15:8]:word_data[7:0]} : word_data;
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.halt_button(halt_button),.debug_block(1'b0),
        .irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(15'b0),.irq_ack(),.waiting(),.peripheral_reset(),
        .mem_addr(address),.mem_write_data(wdata),.mem_request(request),.mem_read(),
        .mem_write(writing),.mem_byte(byte_access),.mem_bank(bank),.mem_physical(physical),
        .mem_ack(ack),.mem_error(error),.mem_read_data(rdata),.stopped(stopped),.fault_code(),
        .retire(),.debug_upc(upc),.debug_uword(),.ir(ir),.mdr(),.psw(),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk) if(!reset)begin
        cycles<=cycles+1;
        if(ack)begin
            if(!bank && dut.engine.fetching)guest_fetches<=guest_fetches+1;
            if(io)begin
                if(!writing && address==16'o177562)rxfull<=0;
                if(writing && address==16'o177566)begin
                    uart={uart,wdata[7:0]};segment={segment,wdata[7:0]};
                    $fwrite(logfile,"%c",wdata[7:0]);$fflush(logfile);
                    suffix={suffix[31:0],wdata[7:0]};if(suffix=="ODT> ")prompts=prompts+1;
                end
                if((address<16'o177560 || address>16'o177566) && address[15:1]!=15'o73000)io_bad<=io_bad+1;
            end else if(writing && !error && !(drop_write && !bank && address==16'o31000))begin
                if(!byte_access)memory[{bank,address[15:1]}]<=wdata;
                else if(address[0])memory[{bank,address[15:1]}][15:8]<=wdata[7:0];
                else memory[{bank,address[15:1]}][7:0]<=wdata[7:0];
            end
        end
        if(stopped || cycles>10000000)$fatal(1,"ODT stopped/timeout PC%o IR%o uPC%h prompts%0d",dut.engine.dp.rf.words[7],ir,upc,prompts);
    end
    task eq(input [15:0] got,want,input string msg);
        begin if(got!==want)$fatal(1,"%s got%o want%o",msg,got,want);checks=checks+1;end
    endtask
    task contains(input string expected);
        integer j;reg found;
        begin
            found=0;
            for(j=0;j+expected.len()<=segment.len();j=j+1)if(segment.substr(j,j+expected.len()-1)==expected)found=1;
            if(!found)$fatal(1,"missing [%s] in [%s]",expected,segment);
            checks=checks+1;
        end
    endtask
    task send(input string s);
        integer j;
        begin
            for(j=0;j<s.len();j=j+1)begin
                wait(!rxfull);@(negedge clk);rxbyte=s[j];rxfull=1;
                wait(!rxfull);repeat(100)@(negedge clk);
            end
        end
    endtask
    task command(input string s);
        begin
            baseprompt=prompts;segment="";send({s,8'd13});wait(prompts>baseprompt);@(negedge clk);
        end
    endtask
    initial begin
        if(!$value$plusargs("UART_LOG=%s",log_path))log_path="build/test-odt/uart.txt";
        logfile=$fopen(log_path,"w");
        for(integer i=0;i<65536;i=i+1)memory[i]=0;
        `include "odt_cases.vh"
        $display("PASS CP64 monitor: %0d checks %0d commands %0d clocks",checks,prompts,cycles);
        $fclose(logfile);$finish;
    end
endmodule
