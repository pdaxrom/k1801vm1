`timescale 1ns/1ps
module tb_stream_engine;
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,rdata,dispatch_ir,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    wire cs_n,sck,mosi,miso;
    integer cycles=0,beats=0,edges=0,k;
    reg [15:0] words[0:3];
    wire prefetch_enable;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_engine dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.dispatch_address(10'h030),.dispatch_ir(dispatch_ir),
        .mem_addr(addr),.mem_write_data(wdata),.mem_request(request),.mem_read(reading),
        .mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),
        .debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data));
    defparam dut.rom.IMAGE="microcode/generated/stream_test.mem";
    wire stream=uword[35] && (uword[34:31]==4'd2 || (uword[34:31]==4'd11 && uword[5]));
    uj11_prefetch memory(.prefetch_enable(prefetch_enable),.clk(clk),.reset(reset),.request(request),.write(writing),.byte_access(byte_access),
        .stream(stream),.address(addr),.wdata(wdata),.pc_write(rf_write && rf_address==4'd7),.pc_data(rf_data),
        .stopped(stopped),.rdata(rdata),.ack(ack),.error(error),.io_request(),.io_write(),.io_byte(),
        .io_address(),.io_wdata(),.io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
        .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #17 clk=~clk;
    always @(posedge sck) if(!cs_n) edges=edges+1;
    always @(posedge clk) if(request && ack) begin
        if(!stream || writing || byte_access || addr!==16'(2*beats) || beats>=4 || rdata!==words[beats])
            $fatal(1,"stream beat %0d addr=%h data=%h",beats,addr,rdata);
        beats=beats+1;
    end
    initial begin
        words[0]=16'o010001; words[1]=16'hbeef; words[2]=16'hfffe; words[3]=16'h80c0;
        #100;
        for(k=0;k<4;k=k+1) begin fram.memory[k*2]=words[k][7:0]; fram.memory[k*2+1]=words[k][15:8]; end
        @(posedge clk); #1; @(negedge clk); reset=0;
        while(!stopped && cycles<500) begin @(posedge clk); #1; cycles=cycles+1; end
        if(!stopped || fault!==3 || beats!=4 || retire || ir!==words[0] || mdr!==words[3] ||
           dut.dp.rf.words[7]!==8 || psw!==16'o340 || fram.transaction_count!=1)
            $fatal(1,"stream microprogram result");
        for(k=0;k<4;k=k+1) if(dut.dp.rf.words[8+k]!==words[k]) $fatal(1,"stream MDR capture T%0d",k);
        repeat(50) begin @(posedge clk); #1; end
        if(!cs_n || memory.buffer_valid) $fatal(1,"stream STOP did not close");
        $display("PASS stream engine: opcode+3 extension words, one READ transaction, %0d clocks, %0d SPI clocks incl discarded lookahead",cycles,edges);
        $finish;
    end
endmodule
