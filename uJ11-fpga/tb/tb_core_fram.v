`timescale 1ns/1ps
module tb_core_fram;
    localparam CASES=12928, FIELDS=21;
    reg clk=0,reset=1;
    wire error;
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
    wire prefetch_enable;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),
        .mem_request(request),.mem_read(reading),.mem_write(writing),.mem_byte(byte_access),
        .mem_ack(ack),.mem_error(error),.mem_read_data(rdata),.stopped(stopped),
        .fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    wire cs_n,sck,mosi,miso,io_request;
    wire [15:0] unused_io_address,unused_io_wdata;
    wire unused_io_write,unused_io_byte;
    reg [31:0] beats=0, write_beats=0;
    uj11_prefetch memory(.prefetch_enable(prefetch_enable),.clk(clk),.reset(reset),.request(request),.write(writing),
        .byte_access(byte_access),.address(addr),.wdata(wdata),.ack(ack),.error(error),.rdata(rdata),
        .stream(uword[35] && uword[34:31]==4'd2),.stopped(stopped),
        .pc_write(rf_write && rf_address==7),.pc_data(rf_data),
        .io_request(io_request),.io_write(unused_io_write),.io_byte(unused_io_byte),
        .io_address(unused_io_address),.io_wdata(unused_io_wdata),
        .io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
        .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    assign transactions=beats;
    assign writes=write_beats;
    always @(posedge clk) begin
        if(reset) begin beats<=0; write_beats<=0; end
        else if(request && ack) begin beats<=beats+1; if(writing) write_beats<=write_beats+1; end
    end
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
            for(k=0;k<8;k=k+1) dut.engine.dp.rf.words[k]=vectors[base+2+k];
            dut.engine.status.psw=vectors[base+1];
            pc=vectors[base+9];
            fram.memory[pc]=vectors[base][7:0];
            fram.memory[pc+16'd1]=vectors[base][15:8];
            cycles=0;
            while(!retire && cycles<200) begin tick; cycles=cycles+1; end
            if(!retire || stopped || fault || cycles!==107)
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
        $display("PASS FRAM differential: %0d DCJ11 cases, R0..R7/PSW/memory/traps; %0d clocks",CASES,total_cycles);
        $finish;
    end
    initial begin #(CASES*1500+1000000); $fatal(1,"core timeout"); end
endmodule
