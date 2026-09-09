`timescale 1ns/1ps
module tb_cp9_bench #(parameter integer MEMORY_MODE=-1, BYTE_VARIANT=0);
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    integer file,w,i,k,pc,n,clocks,beats=0,spi_edges=0,start_beats,start_edges,start_cs;
    integer words_per_instruction,loop_words;
    reg [15:0] opcode,branch,ext0,ext1;
    string workload,output_stem;
    string unary_name[0:11];
    integer bidx,taken,flags,target_count,warmup,opindex;
    reg found;
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
    function condition;
        input integer index,flags_in;
        reg nn,zz,vv,cc;
        begin
            nn=(flags_in&8)!=0;zz=(flags_in&4)!=0;vv=(flags_in&2)!=0;cc=(flags_in&1)!=0;
            case(index)
                1:condition=1;2:condition=!zz;3:condition=zz;4:condition=nn==vv;
                5:condition=nn!=vv;6:condition=!zz && nn==vv;7:condition=zz || nn!=vv;
                8:condition=!nn;9:condition=nn;10:condition=!cc && !zz;11:condition=cc || zz;
                12:condition=!vv;13:condition=vv;14:condition=!cc;15:condition=cc;
                default:condition=0;
            endcase
        end
    endfunction
    function [15:0] branch_opcode;
        input integer index,offset;
        begin branch_opcode=((index<8 ? index : 128+index-8)<<8)|(offset&255);end
    endfunction
    initial begin
        unary_name[0]="CLR";unary_name[1]="COM";unary_name[2]="INC";unary_name[3]="DEC";
        unary_name[4]="NEG";unary_name[5]="ADC";unary_name[6]="SBC";unary_name[7]="TST";
        unary_name[8]="ROR";unary_name[9]="ROL";unary_name[10]="ASR";unary_name[11]="ASL";
        if(BYTE_VARIANT)begin
            output_stem="single-byte";
            for(k=0;k<12;k=k+1)unary_name[k]={unary_name[k],"B"};
        end else output_stem="cp9";
        file=$fopen($sformatf("build/%s-benchmarks-%0d.json",output_stem,MEMORY_MODE),"w");
        $fwrite(file,"[\n");
        #100;
        // 24 unary loops, 29 branch outcomes, 15 taken self-loops, DEC/BNE program.
        for(w=0;w<(BYTE_VARIANT?24:69);w=w+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<65536;k=k+2)poke(k[15:0],16'd0);
            dut.engine.dp.rf.words[1]=w<12?16'h8001:16'h4000;
            poke(16'h4000,16'h8001);dut.engine.status.psw=1;
            warmup=32;target_count=256;pc=0;opcode=0;branch=0;
            if(w<24)begin
                opindex=w%12;opcode=(BYTE_VARIANT?16'o105000:16'o005000)+(opindex<<6)+(w<12?1:9);
                workload=$sformatf("%s_%s",unary_name[opindex],w<12?"reg":"mem");
                for(k=0;k<31;k=k+1)begin poke(pc[15:0],opcode);pc=pc+2;end
                branch=branch_opcode(1,-32);poke(pc[15:0],branch);
            end else if(w<68)begin
                if(w<53)begin
                    if(w==24)begin bidx=1;taken=1;end
                    else begin bidx=2+(w-25)/2;taken=(w-25)%2;end
                    if(taken)workload=$sformatf("branch_%0d_taken_forward",bidx);
                    else workload=$sformatf("branch_%0d_not_taken",bidx);
                end else begin
                    bidx=w-52;taken=1;warmup=1;
                    workload=$sformatf("branch_%0d_taken_self",bidx);
                end
                found=0;flags=0;
                for(k=0;k<16;k=k+1)if(!found && condition(bidx,k)==taken)begin flags=k;found=1;end
                if(!found)$fatal(1,"benchmark condition unavailable");
                dut.engine.status.psw=flags;
                opcode=branch_opcode(bidx,w<53?1:-1);
                if(w<53)begin
                    for(k=0;k<31;k=k+1)begin poke(pc[15:0],opcode);pc=pc+(taken?4:2);end
                    branch=branch_opcode(1,-(pc/2+1));poke(pc[15:0],branch);
                end else begin poke(0,opcode);branch=opcode;end
            end else begin
                workload="countdown_MOV_16_DEC_BNE_BR";warmup=34;target_count=272;
                poke(0,16'o012701);poke(2,16'd16);poke(4,16'o005301);
                poke(6,16'o001376);poke(8,16'o000773);
            end
            n=0;while(n<warmup)begin tick;if(stopped)$fatal(1,"CP9 warmup %s upc%h",workload,upc);if(retire)n=n+1;end
            clocks=0;n=0;start_beats=beats;start_edges=spi_edges;start_cs=cs_count;
            while(n<target_count)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>300000)$fatal(1,"CP9 benchmark %s stopped/timeout",workload);
                if(retire)begin
                    if(w<68 && ir!==opcode && ir!==branch)$fatal(1,"wrong instruction retired %o",ir);
                    if(w==68 && ir!==16'o012701 && ir!==16'o005301 && ir!==16'o001376 && ir!==16'o000773)
                        $fatal(1,"countdown wrong instruction %o",ir);
                    n=n+1;
                end
            end
            if(dut.engine.dp.rf.words[7]!==0)$fatal(1,"CP9 benchmark loop PC %s",workload);
            if(w==68 && dut.engine.dp.rf.words[1]!==0)$fatal(1,"countdown result");
            $display("CP9 BENCH mode%0d %s: %0d clocks / %0d instructions, %0d memory beats, %0d CS, %0d SPI clocks",
                     MEMORY_MODE,workload,clocks,n,beats-start_beats,cs_count-start_cs,spi_edges-start_edges);
            $fwrite(file,"  {\"workload\":\"%s\",\"instructions\":%0d,\"microclocks\":%0d,\"memory_beats\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                    workload,n,clocks,beats-start_beats,cs_count-start_cs,spi_edges-start_edges,w==(BYTE_VARIANT?23:68)?"":",");
        end
        $fwrite(file,"]\n");$fclose(file);
        $display("PASS %s benchmarks mode%0d: %0d workloads, warmup excluded",output_stem,MEMORY_MODE,BYTE_VARIANT?24:69);
        $finish;
    end
    initial begin #100000000;$fatal(1,"CP9 benchmark timeout");end
endmodule
