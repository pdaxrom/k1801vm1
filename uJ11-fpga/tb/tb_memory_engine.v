`timescale 1ns/1ps
module tb_memory_engine;
    reg clk=0,reset=1;
    reg [7:0] wait_states=0;
    wire [15:0] addr,wdata,rdata,dispatch_ir,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [31:0] transactions,writes;
    integer w,cycles;
    uj11_engine dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.dispatch_address(10'h3ff),.dispatch_ir(dispatch_ir),
        .mem_addr(addr),.mem_write_data(wdata),.mem_request(request),.mem_read(reading),
        .mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(1'b0),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),
        .debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data));
    defparam dut.rom.IMAGE="microcode/generated/memory_test.mem";
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(reading),.writing(writing),
        .byte_access(byte_access),.addr(addr),.write_data(wdata),.wait_states(wait_states),
        .ack(ack),.read_data(rdata),.transactions(transactions),.writes(writes));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    task tick; begin @(posedge clk); #1; end endtask
    initial begin
        #100;
        for(w=0;w<8;w=w+1) begin
            @(negedge clk); reset=1; wait_states=w[7:0];
            tick; @(negedge clk); reset=0;
            cycles=0;
            while(!stopped && cycles<100) begin tick; cycles=cycles+1; end
            if(!stopped || fault!==3 || cycles!==10+4*w || transactions!==4 || writes!==2)
                $fatal(1,"memory microcode completion/stalls w=%0d cycles=%0d txn=%0d",w,cycles,transactions);
            if(dut.dp.rf.words[2]!==16'h00a5 || dut.dp.rf.words[3]!==16'h005a ||
               ram.bytes[16'h80]!==8'ha5 || ram.bytes[16'h81]!==8'h5a || mdr!==16'h005a ||
               ir!==0 || psw!==16'o340 || retire)
                $fatal(1,"memory microcode read/write/MDR or architectural side effect");
        end
        $display("PASS memory engine: microcoded word/byte READ/WRITE/MDR, 0..7 waits, exact beat/cycle counts");
        $finish;
    end
    initial begin #20000; $fatal(1,"memory engine timeout"); end
endmodule
