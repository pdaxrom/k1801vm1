`timescale 1ns/1ps
module tb_engine;
    reg clk=0,reset=1,ack=0,error=0;
    reg [15:0] rdata=16'o060102;
    reg [9:0] dispatch=10'h160;
    wire [15:0] addr,wdata,dispatch_ir,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    integer i;
    uj11_engine dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.dispatch_address(dispatch),.dispatch_ir(dispatch_ir),
        .mem_addr(addr),.mem_write_data(wdata),.mem_request(request),.mem_read(reading),
        .mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),
        .debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    task tick; begin @(posedge clk); #1; end endtask
    task start;
        begin
            @(negedge clk); reset=1; ack=0; error=0;
            tick; @(negedge clk); reset=0;
            repeat(17) tick;
            if(upc!==10'h020 || !request || !reading || writing || byte_access)
                $fatal(1,"reset/fetch state");
            for(i=0;i<16;i=i+1)
                if(dut.dp.rf.words[i]!==0) $fatal(1,"RF reset R%0d",i);
        end
    endtask
    initial begin
        #100;
        start;
        @(negedge clk);
        dut.dp.rf.words[1]=16'h7fff; dut.dp.rf.words[2]=1;
        repeat(4) begin
            tick;
            if(upc!==10'h020 || addr!==0 || ir!==0 || mdr!==0 || psw!==16'o340 ||
               dut.dp.rf.words[7]!==0 || rf_write || retire) $fatal(1,"wait changed state");
        end
        @(negedge clk); ack=1;
        tick;
        if(ir!==rdata || mdr!==rdata || upc!==10'h160 || dut.dp.rf.words[7]!==2 || request)
            $fatal(1,"FETCH completion");
        @(negedge clk); ack=0;
        tick;
        if(!retire || dut.dp.rf.words[2]!==16'h8000 || psw!==16'hea || upc!==10'h020)
            $fatal(1,"one-cycle ADD execution");
        // Odd word faults before any request; no PC/IR/PSW write.
        @(negedge clk); dut.dp.rf.words[7]=3;
        #1; if(request) $fatal(1,"odd request escaped");
        tick;
        if(stopped || fault!==0 || upc!==10'h015 || !dut.irq_active || dut.dp.rf.words[7]!==3 || psw!==16'hea)
            $fatal(1,"odd fault state");
        if(dut.seq.link_valid || retire || rf_write && rf_address<8) $fatal(1,"fault committed state");
        start;
        @(negedge clk); ack=1; error=1;
        tick;
        if(stopped || fault!==0 || upc!==10'h015 || ir!==0 || mdr!==0 || dut.dp.rf.words[7]!==0)
            $fatal(1,"bus error committed failed fetch");
        start;
        @(negedge clk); ack=1; dispatch=10'h3ff;
        tick;
        if(!stopped || fault!==3 || dut.dp.rf.words[7]!==2 || psw!==16'o340)
            $fatal(1,"unsupported dispatch");
        $display("PASS engine: RF reset, fetch waits, single-cycle ADD, odd/bus redirect without commit, explicit STOP");
        $finish;
    end
    initial begin #20000; $fatal(1,"engine timeout"); end
endmodule
