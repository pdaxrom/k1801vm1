`timescale 1ns/1ps
module tb_eis_div_bench #(parameter integer MEMORY_MODE=-1);
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    integer file,w,i,k,pc,n,clocks,beats=0,spi_edges=0,start_beats,start_edges,start_cs;
    reg [15:0] opcode,vector_address;
    string workload,operand_kind;
    integer target_count,warmup,kind,last_retire_clock,div_clocks,div_count;
    reg [15:0] expected_r0,expected_r1,expected_psw,initial_high,divisor_bits;
    reg signed [63:0] dividend,divisor,quotient,remainder;
    reg [31:0] dividend_bits;reg [15:0] initial_low;
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
    function [15:0] operand_address;
        input integer mode,index;
        begin
            case(mode)
                2:operand_address=16'h4000+16'(index*2);
                3,5:operand_address=16'h5000+16'(index*2);
                4:operand_address=16'h3ffe-16'(index*2);
                6:operand_address=16'h4020;
                7:operand_address=16'h5000;
                default:operand_address=16'h4000;
            endcase
        end
    endfunction
    initial begin
        file=$fopen($sformatf("build/eis-div-benchmarks-%0d.json",MEMORY_MODE),"w");
        $fwrite(file,"[\n");#100;
        for(w=0;w<16;w=w+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<65536;k=k+2)poke(k[15:0],0);
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h8765+16'(k);
            kind=w<8?w:0;
            case(w)
                8:begin dividend_bits=0;divisor_bits=16'h8000;end
                9:begin dividend_bits=32'h00007fff;divisor_bits=1;end
                10:begin dividend_bits=32'hffff8000;divisor_bits=1;end
                11:begin dividend_bits=32'h00008000;divisor_bits=1;end
                12:begin dividend_bits=32'hffff7fff;divisor_bits=1;end
                13:begin dividend_bits=32'h80000000;divisor_bits=16'hffff;end
                14:begin dividend_bits=32'hffffffff;divisor_bits=0;end
                15:begin dividend_bits=32'h00000001;divisor_bits=16'h8000;end
                default:begin dividend_bits=32'hfffedcbb;divisor_bits=16'h0123;end
            endcase
            initial_high=dividend_bits[31:16];initial_low=dividend_bits[15:0];
            dividend=$signed(dividend_bits);divisor=$signed(divisor_bits);
            expected_r0=initial_high;expected_r1=initial_low;
            if(divisor==0)expected_psw=16'h00e7;
            else begin
                quotient=dividend/divisor;remainder=dividend%divisor;
                if(quotient < -32768 || quotient > 32767)expected_psw=16'h00e2 | (quotient<0?16'd8:16'd0);
                else begin
                    expected_r0=quotient[15:0];expected_r1=remainder[15:0];
                    expected_psw=16'h00e0 | (quotient<0?16'd8:16'd0) | (quotient==0?16'd4:16'd0);
                end
            end
            dut.engine.dp.rf.words[0]=initial_high;
            dut.engine.dp.rf.words[1]=initial_low;
            dut.engine.dp.rf.words[3]=divisor_bits;
            dut.engine.dp.rf.words[2]=16'h4000;
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.status.psw=16'h00ef;
            for(k=0;k<15;k=k+1)begin
                poke(operand_address(kind,k),divisor_bits);
                if(kind==3)poke(16'h4000+16'(k*2),operand_address(kind,k));
                if(kind==5)poke(16'h3ffe-16'(k*2),operand_address(kind,k));
            end
            if(kind==7)poke(16'h4020,16'h5000);
            opcode=kind==0 ? 16'o071003 : 16'o071002 | (16'(kind)<<3);
            if(w<8)workload=$sformatf("DIV_source_mode%0d",kind);
            else workload=$sformatf("DIV_RR_%08h_by_%04h",dividend_bits,divisor_bits);
            warmup=(kind>=2 && kind<=5)?47:46;target_count=8*warmup;pc=0;
            for(k=0;k<15;k=k+1)begin
                poke(16'(pc),16'o012700);poke(16'(pc+2),initial_high);pc=pc+4;
                poke(16'(pc),16'o012701);poke(16'(pc+2),initial_low);pc=pc+4;
                poke(16'(pc),opcode);pc=pc+2;
                if(kind>=6)begin poke(16'(pc),16'h0020);pc=pc+2;end
            end
            // Restore the EA register once per loop for autoincrement/decrement.
            // This MOV and the BR are included in retired-instruction counts.
            if(kind>=2 && kind<=5)begin poke(16'(pc),16'o012702);poke(16'(pc+2),16'h4000);pc=pc+4;end
            poke(16'(pc),16'o000400 | (16'(255-pc/2) & 16'h00ff));
            n=0;clocks=0;
            while(n<warmup)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"DIV warmup %s pc%h upc%h",workload,dut.engine.dp.rf.words[7],upc);
                if(retire)n=n+1;
            end
            clocks=0;n=0;last_retire_clock=0;div_clocks=0;div_count=0;start_beats=beats;start_edges=spi_edges;start_cs=cs_count;
            while(n<target_count)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"DIV benchmark %s pc%h upc%h",workload,dut.engine.dp.rf.words[7],upc);
                if(retire)begin
                    n=n+1;
                    if(ir[15:9]==7'o71)begin div_count=div_count+1;div_clocks=div_clocks+clocks-last_retire_clock;end
                    last_retire_clock=clocks;
                end
            end
            if(div_count!=120)$fatal(1,"DIV retirement count %0d",div_count);
            if(kind>=2 && kind<=5)expected_psw=16'h00e0 | (expected_psw & 16'd1);
            if(dut.engine.dp.rf.words[7]!==0 || dut.engine.dp.rf.words[6]!==16'h6000 || psw!==expected_psw)
                $fatal(1,"DIV loop PC/SP/PSW %s %h/%h",workload,psw,expected_psw);
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==
                (k==0?expected_r0:k==1?expected_r1:k==2?16'h4000:k==3?divisor_bits:16'h8765+16'(k)))
                $fatal(1,"DIV loop register %s R%0d",workload,k);
            if(kind!=0)for(k=0;k<15;k=k+1)begin
                vector_address=operand_address(kind,k);
                if(MEMORY_MODE<0)begin
                    if({ram_mode.ram.bytes[vector_address+16'd1],ram_mode.ram.bytes[vector_address]}!==divisor_bits)$fatal(1,"DIV RAM result mode%0d operand%0d",w,k);
                end else begin
                    if({fram_mode.fram.memory[vector_address+16'd1],fram_mode.fram.memory[vector_address]}!==divisor_bits)$fatal(1,"DIV FRAM result mode%0d operand%0d",w,k);
                end
            end
            if(irq_ack || waiting || fault)$fatal(1,"unexpected control state");
            $fwrite(file,"  {\"workload\":\"%s\",\"instructions\":%0d,\"microclocks\":%0d,\"div_instructions\":%0d,\"div_retire_clocks\":%0d,\"memory_beats\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                workload,target_count,clocks,div_count,div_clocks,beats-start_beats,cs_count-start_cs,spi_edges-start_edges,w==15 ? "" : ",");
            $display("DIV BENCH mode%0d %s: %0d clocks / %0d instructions, %0d memory beats, %0d CS, %0d SPI clocks",
                MEMORY_MODE,workload,clocks,target_count,beats-start_beats,cs_count-start_cs,spi_edges-start_edges);
        end
        $fwrite(file,"]\n");$fclose(file);
        $display("PASS DIV benchmarks mode%0d: 16 workloads, warmup excluded",MEMORY_MODE);$finish;
    end
    initial begin #300000000;$fatal(1,"DIV benchmark timeout");end
endmodule
