`timescale 1ns/1ps
module tb_ea_bench #(parameter integer MEMORY_MODE=-1, BYTE_VARIANT=0, MOVB_ONLY=0);
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    integer file,w,i,k,pc,n,clocks,beats=0,spi_edges=0,start_beats,start_edges,start_cs;
    integer words_per_instruction,loop_words;
    string output_stem;
    reg [15:0] opcode,branch,ext0,ext1;
    string names[0:24];
    wire prefetch_enable;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
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
        names[0]="MOV_mem_reg"; names[1]="MOV_reg_mem"; names[2]="MOV_mem_mem";
        names[3]="ADD_mem_reg"; names[4]="ADD_reg_mem"; names[5]="CMP_mem_mem";
        names[6]="MOV_immediate_reg"; names[7]="MOV_indexed_reg"; names[8]="MOV_mem_indexed";
        names[9]="MOV_indexed_indexed"; names[10]="MOV_autoinc_autoinc";
        names[11]="MOV_stack_push"; names[12]="MOV_absolute_absolute";
        names[13]="BIT_mem_reg";
        names[14]="BIT_reg_mem";
        names[15]="BIT_mem_mem";
        names[16]="BIC_mem_reg";
        names[17]="BIC_reg_mem";
        names[18]="BIC_mem_mem";
        names[19]="BIS_mem_reg";
        names[20]="BIS_reg_mem";
        names[21]="BIS_mem_mem";
        names[22]="SUB_mem_reg";
        names[23]="SUB_reg_mem";
        names[24]="SUB_mem_mem";
        if(!BYTE_VARIANT)output_stem="ea";else if(MOVB_ONLY)output_stem="movb";else output_stem="byte";
        file=$fopen($sformatf("build/%s-benchmarks-%0d.json",output_stem,MEMORY_MODE),"w");
        $fwrite(file,"[\n");
        #100;
        for(w=0;w<25;w=w+1) if(!BYTE_VARIANT || (w!=3 && w!=4 && w<22 && (!MOVB_ONLY || (w<13 && w!=5)))) begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;
            repeat(17)tick;
            @(negedge clk);
            for(k=0;k<65536;k=k+2)poke(k[15:0],(k>=8192 && k<12288) ? 16'd1 : 16'd0);
            dut.engine.dp.rf.words[1]=16'h2000;dut.engine.dp.rf.words[2]=16'h4000;
            dut.engine.dp.rf.words[6]=16'h6000;
            words_per_instruction=1;ext0=0;ext1=0;
            case(w)
                0: opcode=16'o011102;
                1: opcode=16'o010112;
                2: opcode=16'o011112;
                3: opcode=16'o061102;
                4: opcode=16'o060112;
                5: opcode=16'o021112;
                6: begin opcode=16'o012702;words_per_instruction=2;ext0=16'h5a5a;end
                7: begin opcode=16'o016102;words_per_instruction=2;end
                8: begin opcode=16'o011162;words_per_instruction=2;end
                9: begin opcode=16'o016162;words_per_instruction=3;end
                10: opcode=16'o012122;
                11: opcode=16'o010146;
                12: begin opcode=16'o013737;words_per_instruction=3;ext0=16'h2000;ext1=16'h4000;end
                13: opcode=16'o031102;
                14: opcode=16'o030112;
                15: opcode=16'o031112;
                16: opcode=16'o041102;
                17: opcode=16'o040112;
                18: opcode=16'o041112;
                19: opcode=16'o051102;
                20: opcode=16'o050112;
                21: opcode=16'o051112;
                22: opcode=16'o161102;
                23: opcode=16'o160112;
                24: opcode=16'o161112;
            endcase
            if(BYTE_VARIANT)begin opcode=opcode|16'h8000;names[w]={names[w],"_byte"};end
            pc=0;
            for(k=0;k<31;k=k+1) begin
                poke(pc[15:0],opcode);pc=pc+2;
                if(words_per_instruction>1)begin poke(pc[15:0],ext0);pc=pc+2;end
                if(words_per_instruction>2)begin poke(pc[15:0],ext1);pc=pc+2;end
            end
            loop_words=pc/2+1;branch=16'h0100|((256-loop_words)&255);poke(pc[15:0],branch);
            n=0;while(n<32)begin tick;if(stopped)$fatal(1,"EA benchmark warmup %s upc%h",names[w],upc);if(retire)n=n+1;end
            clocks=0;n=0;start_beats=beats;start_edges=spi_edges;start_cs=cs_count;
            while(n<256)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"EA benchmark %s stopped/timeout",names[w]);
                if(retire)begin
                    if(ir!==opcode && ir!==branch)$fatal(1,"wrong instruction retired %o",ir);
                    n=n+1;
                end
            end
            if(dut.engine.dp.rf.words[7]!==0)$fatal(1,"EA benchmark loop PC");
            $display("EA BENCH mode%0d %s: %0d clocks / %0d instructions, %0d memory beats, %0d CS, %0d SPI clocks",
                     MEMORY_MODE,names[w],clocks,n,beats-start_beats,cs_count-start_cs,spi_edges-start_edges);
            $fwrite(file,"  {\"workload\":\"%s\",\"instructions\":%0d,\"microclocks\":%0d,\"memory_beats\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                    names[w],n,clocks,beats-start_beats,cs_count-start_cs,spi_edges-start_edges,w==(BYTE_VARIANT?(MOVB_ONLY?12:21):24)?"":",");
        end
        $fwrite(file,"]\n");$fclose(file);
        $display("PASS %s benchmarks mode%0d: %0d loops x 256 retirements, 32 warmup excluded",output_stem,MEMORY_MODE,BYTE_VARIANT?(MOVB_ONLY?10:20):25);
        $finish;
    end
    initial begin #50000000;$fatal(1,"EA benchmark timeout");end
endmodule
