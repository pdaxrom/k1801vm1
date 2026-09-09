`timescale 1ns/1ps
module tb_core #(parameter integer ROM_DECODE=0);
    localparam CASES=12928, FIELDS=21;
    reg clk=0,reset=1,error=0;
    reg [7:0] wait_states=0;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [31:0] transactions,writes;
    reg [15:0] vectors[0:CASES*FIELDS-1];
    integer c,k,base,cycles,total_cycles=0;
    reg [15:0] pc;
    uj11_core #(.ROM_DECODE(ROM_DECODE)) dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),
        .mem_request(request),.mem_read(reading),.mem_write(writing),.mem_byte(byte_access),
        .mem_ack(ack),.mem_error(error),.mem_read_data(rdata),.stopped(stopped),
        .fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(reading),.writing(writing),
        .byte_access(byte_access),.addr(addr),.write_data(wdata),.wait_states(wait_states),
        .ack(ack),.read_data(rdata),.transactions(transactions),.writes(writes));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    task tick; begin @(posedge clk); #1; end endtask
    task start;
        begin
            @(negedge clk); reset=1;
            tick; @(negedge clk); reset=0;
            repeat(17) tick;
            if(upc!==10'h020) $fatal(1,"reset microprogram");
        end
    endtask
    initial begin
        $readmemh("build/isa_vectors.mem",vectors);
        #100;
        for(c=0;c<CASES;c=c+1) begin
            start;
            base=c*FIELDS;
            @(negedge clk);
            wait_states=c%4;
            for(k=0;k<8;k=k+1) dut.engine.dp.rf.words[k]=vectors[base+2+k];
            dut.engine.status.psw=vectors[base+1];
            pc=vectors[base+9];
            ram.bytes[pc]=vectors[base][7:0];
            ram.bytes[pc+16'd1]=vectors[base][15:8];
            cycles=0;
            while(!retire && cycles<12) begin tick; cycles=cycles+1; end
            if(!retire || stopped || fault || cycles!==2+wait_states+ROM_DECODE)
                $fatal(1,"case%0d opcode%o retirement/fault/cycles=%0d",c,vectors[base],cycles);
            total_cycles=total_cycles+cycles;
            for(k=0;k<8;k=k+1)
                if(dut.engine.dp.rf.words[k]!==vectors[base+11+k])
                    $fatal(1,"case%0d opcode%o R%0d got%h expected%h",c,vectors[base],k,
                           dut.engine.dp.rf.words[k],vectors[base+11+k]);
            if(psw!==vectors[base+10])
                $fatal(1,"case%0d opcode%o PSW=%h expected=%h",c,vectors[base],psw,vectors[base+10]);
            if(writes!==0 || transactions!==1 || vectors[base+19]!==0 || vectors[base+20]!==0)
                $fatal(1,"case%0d memory/trap mismatch",c);
        end
        // Fetch and PC increment wrap within 16 bits, without an address extension.
        start;
        @(negedge clk); wait_states=0; dut.engine.dp.rf.words[7]=16'hfffe;
        ram.bytes[16'hfffe]=8'h00; ram.bytes[16'hffff]=8'h01; // BR +0
        tick; tick; if(ROM_DECODE!=0)tick;
        if(!retire || dut.engine.dp.rf.words[7]!==0 || transactions!==1)
            $fatal(1,"16-bit PC wrap");
        // Reserved/unsupported instruction frames have their own oracle suite.
        $display("PASS differential: %0d DCJ11 cases, R0..R7/PSW/memory/traps; 0..3 waits, %0d clocks",CASES,total_cycles);
        $display("PASS core boundaries: 16-bit PC wrap");
        $finish;
    end
    initial begin #3500000; $fatal(1,"core timeout"); end
endmodule
