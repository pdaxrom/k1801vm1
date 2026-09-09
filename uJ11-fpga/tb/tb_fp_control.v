`timescale 1ns/1ps
module tb_fp_control #(parameter integer CASES=90112,BENCHMARK=0);
    localparam FIELDS=21;
    string vectors_path;
    reg clk=0,reset=1;
    wire request,rd,wr,byte_access,ack,stopped,retire;
    wire [15:0] address,wdata,rdata,psw;
    wire [9:0] upc;
    wire [1:0] fault;
    reg [7:0] waits=0;
    wire [31:0] transactions,writes;
    reg [15:0] vectors[0:CASES*FIELDS-1];
    integer c,k,b,cycles,max_cycles=0;
    reg [31:0] before_beats;
    reg [63:0] total_cycles=0;
    always #5 clk=~clk;
    uj11_core #(.ROM_DECODE(1),.FP11_CONTROL(1)) dut(.clk(clk),.reset(reset),
        .irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),
        .mem_addr(address),.mem_write_data(wdata),.mem_request(request),.mem_read(rd),.mem_write(wr),
        .mem_byte(byte_access),.mem_ack(ack),.mem_error(1'b0),.mem_read_data(rdata),
        .stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(),
        .ir(),.mdr(),.psw(psw),.q(),.debug_rf_write(),.debug_rf_address(),.debug_rf_data());
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(rd),.writing(wr),
        .byte_access(byte_access),.addr(address),.write_data(wdata),.wait_states(waits),
        .ack(ack),.read_data(rdata),.transactions(transactions),.writes(writes));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    task execute;
        begin
            cycles=0;
            begin: until_retire
                repeat(50)begin
                    @(posedge clk);#1;cycles=cycles+1;
                    if(stopped)$fatal(1,"stop case %0d upc %h fault %h",c,upc,fault);
                    if(retire)disable until_retire;
                end
                $fatal(1,"instruction timeout case %0d upc %h",c,upc);
            end
        end
    endtask
    initial begin
        if(!$value$plusargs("VECTORS=%s",vectors_path))vectors_path="build/fp-control-vectors.mem";
        $readmemh(vectors_path,vectors);#100;
        for(c=0;c<CASES;c=c+1)begin
            b=c*FIELDS;
            @(negedge clk);reset=1;
            @(posedge clk);#1;
            @(negedge clk);reset=0;waits=BENCHMARK!=0 ? 0 : c%4;
            repeat(17)begin @(posedge clk);#1;end
            if(upc!==10'h020)$fatal(1,"reset");
            // Establish FPS through an actual LDFPS, not a hierarchical RAM write.
            @(negedge clk);dut.engine.dp.rf.words[0]=vectors[b+1];
            dut.engine.dp.rf.words[7]=16'o400;
            ram.bytes[16'o400]=8'o100;ram.bytes[16'o401]=8'hf0;
            execute;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=vectors[b+3+k];
            dut.engine.status.psw=vectors[b+2];
            ram.bytes[16'o1000]=vectors[b][7:0];ram.bytes[16'o1001]=vectors[b][15:8];
            before_beats=transactions;
            execute;
            total_cycles=total_cycles+cycles;if(cycles>max_cycles)max_cycles=cycles;
            if(BENCHMARK!=0)$display("FPBENCH %06o %0d %0d",vectors[b],cycles,transactions-before_beats);
            if(psw!==vectors[b+12])$fatal(1,"PSW case %0d op %o got %h expected %h",c,vectors[b],psw,vectors[b+12]);
            for(k=0;k<8;k=k+1)if(dut.engine.dp.rf.words[k]!==vectors[b+13+k])
                $fatal(1,"R%0d case %0d op %o got %h expected %h",k,c,vectors[b],dut.engine.dp.rf.words[k],vectors[b+13+k]);
            if(writes!==0 || transactions-before_beats!==1)$fatal(1,"private traffic escaped to RAM case %0d",c);
            // Observe resulting FPS through an actual STFPS R0.
            @(negedge clk);dut.engine.dp.rf.words[7]=16'o400;
            ram.bytes[16'o400]=8'o200;ram.bytes[16'o401]=8'hf0;
            execute;
            if(dut.engine.dp.rf.words[0]!==vectors[b+11])$fatal(1,"FPS case %0d op %o got %h expected %h",c,vectors[b],dut.engine.dp.rf.words[0],vectors[b+11]);
        end
        $display("PASS FP11 control differential: %0d cases, all LDFPS values, all Rn including PC/SP, PSW/RF/FPS, no external data beats; cycles=%0d max=%0d",CASES,total_cycles,max_cycles);
        $finish;
    end
    initial begin #100000000;$fatal(1,"global timeout");end
endmodule
