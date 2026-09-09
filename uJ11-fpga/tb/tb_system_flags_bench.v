`timescale 1ns/1ps
module tb_system_flags_bench #(parameter integer MEMORY_MODE=-1);
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    integer file,w,i,k,pc,n,clocks,beats=0,spi_edges=0,start_beats,start_edges,start_cs;
    reg [15:0] opcode,vector_address;
    string workload;
    integer target_count,warmup;
    wire prefetch_enable,irq_ack,waiting;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
        .mem_read(reading),.mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),
        .debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    uj11_stream hint(.uword(uword),.ir(ir),.stream(stream));
    generate if(MEMORY_MODE<0) begin : ram_mode
        wire [31:0] unused_tx,unused_wr;
        assign error=0;
        uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(reading),.writing(writing),
            .byte_access(byte_access),.addr(addr),.write_data(wdata),.wait_states(waits),.ack(ack),
            .read_data(rdata),.transactions(unused_tx),.writes(unused_wr));
    end else begin : fram_mode
        wire cs_n,sck,mosi,miso,io_request;
        if(MEMORY_MODE==0) begin : baseline
            uj11_fram_baseline memory(.clk(clk),.reset(reset),.request(request),.write(writing),
                .byte_access(byte_access),.address(addr),.wdata(wdata),.rdata(rdata),.ack(ack),.error(error),
                .io_request(io_request),.io_write(),.io_byte(),.io_address(),.io_wdata(),
                .io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
                .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
        end else begin : sequential
            uj11_prefetch #(.PREFETCH(MEMORY_MODE==2)) memory(.prefetch_enable(prefetch_enable),.clk(clk),.reset(reset),.request(request),.write(writing),
                .byte_access(byte_access),.stream(stream),.address(addr),.wdata(wdata),.rdata(rdata),.ack(ack),.error(error),
                .pc_write(rf_write && rf_address==4'd7),.pc_data(rf_data),.stopped(stopped),
                .io_request(io_request),.io_write(),.io_byte(),.io_address(),.io_wdata(),
                .io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
                .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
        end
        spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    end endgenerate
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    function [15:0] base_word;
        input [15:0] a;
        begin base_word=16'h8000|((a^(a>>3)^16'h3456)&16'h1ffe); end
    endfunction
    wire [31:0] cs_count;
    generate if(MEMORY_MODE>=0) begin : counters
        assign cs_count=fram_mode.fram.transaction_count;
        always @(posedge fram_mode.sck) if(!fram_mode.cs_n) spi_edges=spi_edges+1;
    end else begin : no_spi
        assign cs_count=0;
    end endgenerate
    always @(posedge clk) if(request && ack) beats=beats+1;
    task poke;
        input [15:0] a,v;
        begin
            if(MEMORY_MODE<0) begin
                ram_mode.ram.bytes[a]=v[7:0];ram_mode.ram.bytes[a+16'd1]=v[15:8];
            end else begin
                fram_mode.fram.memory[a]=v[7:0];fram_mode.fram.memory[a+16'd1]=v[15:8];
            end
        end
    endtask
    task tick; begin @(posedge clk); #1; end endtask
    initial begin
        file=$fopen($sformatf("build/system-flags-benchmarks-%0d.json",MEMORY_MODE),"w");
        $fwrite(file,"[\n");#100;
        for(w=0;w<5;w=w+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<65536;k=k+2)poke(k[15:0],0);
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h8765+16'(k);
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.status.psw=16'h00ef;
            case(w)
                0:begin workload="NOP_clear";opcode=16'o000240;end
                1:begin workload="NOP_set";opcode=16'o000260;end
                2:begin workload="CC_all_masks";opcode=16'o000240;end
                3:begin workload="MFPT";opcode=16'o000007;end
                4:begin workload="CC_branch_dependencies";opcode=0;end
            endcase
            warmup=32;target_count=256;
            if(w==4)begin
                poke(0,16'o000261);poke(2,16'o103401);poke(4,16'o075040);
                poke(6,16'o000241);poke(8,16'o103001);poke(10,16'o075040);
                poke(12,16'o000771);warmup=5;target_count=320;
            end else begin
                for(k=0;k<(w==2?32:31);k=k+1)poke(16'(k*2),opcode+(w==2?16'(k):16'b0));
                poke(16'(k*2),16'o000400 | (16'(255-k) & 16'h00ff));
                if(w==2)begin warmup=33;target_count=264;end
            end
            n=0;clocks=0;
            while(n<warmup)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"system flags warmup %s pc%h upc%h",workload,dut.engine.dp.rf.words[7],upc);
                if(retire)n=n+1;
            end
            clocks=0;n=0;start_beats=beats;start_edges=spi_edges;start_cs=cs_count;
            while(n<target_count)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"system flags benchmark %s pc%h upc%h",workload,dut.engine.dp.rf.words[7],upc);
                if(retire)n=n+1;
            end
            if(dut.engine.dp.rf.words[7]!==0 || dut.engine.dp.rf.words[6]!==16'h6000 || psw!==(w==4?16'h00ee:16'h00ef))
                $fatal(1,"system flags loop PC/SP/PSW %s %h",workload,psw);
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==((w==3 && k==0)?16'd5:16'h8765+16'(k)))
                $fatal(1,"system flags loop register %s R%0d",workload,k);
            if(irq_ack || waiting || fault)$fatal(1,"unexpected control state");
            $fwrite(file,"  {\"workload\":\"%s\",\"instructions\":%0d,\"microclocks\":%0d,\"memory_beats\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                workload,target_count,clocks,beats-start_beats,cs_count-start_cs,spi_edges-start_edges,w==4 ? "" : ",");
            $display("SYSTEM FLAGS BENCH mode%0d %s: %0d clocks / %0d instructions, %0d memory beats, %0d CS, %0d SPI clocks",
                MEMORY_MODE,workload,clocks,target_count,beats-start_beats,cs_count-start_cs,spi_edges-start_edges);
        end
        $fwrite(file,"]\n");$fclose(file);
        $display("PASS system flags benchmarks mode%0d: 5 workloads, warmup excluded",MEMORY_MODE);$finish;
    end
    initial begin #300000000;$fatal(1,"system flags benchmark timeout");end
endmodule
