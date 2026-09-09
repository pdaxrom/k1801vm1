`timescale 1ns/1ps
module tb_ea #(parameter integer ROM_DECODE=0, MEMORY_MODE=-1, SUITE=0);
    reg clk=0,reset=1,irq_valid=0;
    reg [15:0] irq_request=0;
    wire irq_ack,waiting;
    integer expect_irq=0,irq_seen=0,retired=0;
    wire finished=(SUITE==9 && expect_irq!=0) ?
                  (irq_seen==1 && upc==10'h020 && !dut.engine.irq_active) : retire;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    reg [15:0] pre[0:7],post[0:7],patch_a[0:7],patch_v[0:7];
    reg [15:0] ta[0:31],td[0:31],tw[0:31];
    reg [15:0] op,prepsw,postpsw,value;
    integer f,rc,id,np,nt,i,k,cycles,beat,checks=0,total_cycles=0,total_beats=0,active=0;
    integer coverage[0:15][0:7][0:7],cls;
    integer cases_file;
    integer single_coverage[0:63][0:7];
    integer branch_coverage[0:15][0:15];
    integer jump_coverage[0:7][0:7],jsr_coverage[0:7][0:7][0:7];
    integer rts_coverage[0:7],sob_coverage[0:7][0:63];
    integer mark_coverage[0:63];
    integer trap_coverage[0:4],trap_code_coverage[0:1][0:255];
    integer rti_ipl_coverage[0:7][0:7];
    string suite_name;
    wire prefetch_enable;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_core #(.ROM_DECODE(ROM_DECODE)) dut(.irq_valid(irq_valid),.irq_priority(irq_request[11:9]),.irq_vector(irq_request[8:1]),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
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
    wire [15:0] bus_value=writing?wdata:rdata;
    always @(posedge clk) if(active && request && ack) begin
        if(beat>=nt || addr!==ta[beat] || writing!==tw[beat][0] || byte_access!==tw[beat][1] ||
           (byte_access ? bus_value[7:0]!==td[beat][7:0] : bus_value!==td[beat]))
            $fatal(1,"case%0d op%o beat%0d got wr%b %h:%h expected wr%h %h:%h upc%h",id,op,beat,
                   writing,addr,writing?wdata:rdata,tw[beat],ta[beat],td[beat],upc);
        beat=beat+1;
    end
    always @(posedge clk)begin
        if(reset)begin irq_seen=0;retired=0;irq_valid<=0;end
        else begin
            if(retire)retired=retired+1;
            if(irq_ack)begin
                if(!active || irq_seen!=0)$fatal(1,"duplicate/unexpected IRQ ack");
                irq_seen=irq_seen+1;irq_valid<=0;
            end
        end
    end
    initial begin
        #100;
        case(SUITE)
            0:suite_name="ea";1:suite_name="single";2:suite_name="branch";
            3:suite_name="movb";4:suite_name="byte";5:suite_name="single_byte";
            6:suite_name="control";7:suite_name="extra";8:suite_name="trap";9:suite_name="irq";10:suite_name="illegal";
            default:$fatal(1,"invalid differential suite");
        endcase
        f=$fopen({"build/",suite_name,"-vectors.txt"},"r");if(!f)$fatal(1,"missing vectors");
        cases_file=$fopen($sformatf("build/%s-cycles-%0d.csv",suite_name,MEMORY_MODE),"w");
        $fwrite(cases_file,"case,opcode,wait_clocks,microclocks,memory_beats\n");
        for(i=0;i<65536;i=i+2)poke(i[15:0],base_word(i[15:0]));
        for(cls=0;cls<16;cls=cls+1)for(i=0;i<8;i=i+1)for(k=0;k<8;k=k+1)coverage[cls][i][k]=0;
        for(i=0;i<64;i=i+1)for(k=0;k<8;k=k+1)single_coverage[i][k]=0;
        for(i=0;i<16;i=i+1)for(k=0;k<16;k=k+1)branch_coverage[i][k]=0;
        for(i=0;i<8;i=i+1)begin
            rts_coverage[i]=0;
            for(k=0;k<64;k=k+1)sob_coverage[i][k]=0;
            for(k=0;k<8;k=k+1)begin
                jump_coverage[i][k]=0;
                for(cls=0;cls<8;cls=cls+1)jsr_coverage[cls][i][k]=0;
            end
        end
        for(i=0;i<64;i=i+1)mark_coverage[i]=0;
        for(i=0;i<5;i=i+1)trap_coverage[i]=0;
        for(i=0;i<2;i=i+1)for(k=0;k<256;k=k+1)trap_code_coverage[i][k]=0;
        for(i=0;i<8;i=i+1)for(k=0;k<8;k=k+1)rti_ipl_coverage[i][k]=0;
        rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        while(rc==3) begin
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",pre[k]);
            rc=$fscanf(f,"%h",np);if(np>8)$fatal(1,"patch overflow");
            if(SUITE==9)rc=$fscanf(f,"%h %h",irq_request,expect_irq);
            for(k=0;k<np;k=k+1)rc=$fscanf(f,"%h %h",patch_a[k],patch_v[k]);
            rc=$fscanf(f,"%h",postpsw);
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",post[k]);
            rc=$fscanf(f,"%h",nt);if(nt>32)$fatal(1,"trace overflow");
            for(k=0;k<nt;k=k+1)rc=$fscanf(f,"%h %h %h",tw[k],ta[k],td[k]);
            @(negedge clk); reset=1; active=0; tick; @(negedge clk); reset=0;
            repeat(17)tick;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=pre[k];
            dut.engine.status.psw=prepsw;
            for(k=0;k<np;k=k+1)poke(patch_a[k],patch_v[k]);
            beat=0;active=1;cycles=0;waits=id%4;
            irq_valid=(SUITE==9 && irq_request!=0);
            while(!finished && !stopped && cycles<1500)begin tick;cycles=cycles+1;end
            if(!finished || stopped || fault || beat!=nt || psw!==postpsw)
                $fatal(1,"case%0d op%o completion pc%h upc%h clocks%0d beats%0d/%0d flags%h/%h fault%h",id,op,
                       dut.engine.dp.rf.words[7],upc,cycles,beat,nt,psw,postpsw,fault);
            for(k=0;k<8;k=k+1)if(dut.engine.dp.rf.words[k]!==post[k])
                $fatal(1,"case%0d op%o R%0d got%h expected%h",id,op,k,dut.engine.dp.rf.words[k],post[k]);
            if(SUITE==9)begin
                if(irq_seen!=expect_irq || retired+(retire?1:0)!=1)$fatal(1,"IRQ ack/retire accounting case%0d",id);
                if(op==1 && !expect_irq && !waiting)$fatal(1,"masked WAIT did not wait");
                if(waiting && expect_irq)$fatal(1,"accepted IRQ left WAIT active");
            end
            active=0;
            cls=op[15:12];
            coverage[cls][op[11:9]][op[5:3]]=coverage[cls][op[11:9]][op[5:3]]+1;
            single_coverage[op[11:6]][op[5:3]]=single_coverage[op[11:6]][op[5:3]]+1;
            branch_coverage[{op[15],op[10:8]}][prepsw[3:0]]=branch_coverage[{op[15],op[10:8]}][prepsw[3:0]]+1;
            if(SUITE==6)begin
                if((op&16'o177700)==16'o000100)jump_coverage[op[5:3]][op[2:0]]=jump_coverage[op[5:3]][op[2:0]]+1;
                else if((op&16'o177000)==16'o004000)jsr_coverage[op[8:6]][op[5:3]][op[2:0]]=jsr_coverage[op[8:6]][op[5:3]][op[2:0]]+1;
                else if((op&16'o177770)==16'o000200)rts_coverage[op[2:0]]=rts_coverage[op[2:0]]+1;
                else if((op&16'o177000)==16'o077000)sob_coverage[op[8:6]][op[5:0]]=sob_coverage[op[8:6]][op[5:0]]+1;
                else $fatal(1,"invalid control fixture");
            end
            if(SUITE==7 && (op & 16'o177700)==16'o006400)mark_coverage[op[5:0]]=mark_coverage[op[5:0]]+1;
            if(SUITE==8)begin
                if((prepsw & 16'hff10)!=0 || (postpsw & 16'hff10)!=0)$fatal(1,"trap fixture outside kernel/T=0 profile");
                if(op==2)begin
                    trap_coverage[4]=trap_coverage[4]+1;
                    rti_ipl_coverage[prepsw[7:5]][postpsw[7:5]]=rti_ipl_coverage[prepsw[7:5]][postpsw[7:5]]+1;
                end else if(op==3)trap_coverage[0]=trap_coverage[0]+1;
                else if(op==4)trap_coverage[1]=trap_coverage[1]+1;
                else if((op & 16'o177000)==16'o104000)begin
                    trap_coverage[2+op[8]]=trap_coverage[2+op[8]]+1;
                    trap_code_coverage[op[8]][op[7:0]]=trap_code_coverage[op[8]][op[7:0]]+1;
                end else $fatal(1,"invalid trap fixture");
            end
            checks=checks+1;total_cycles=total_cycles+cycles;total_beats=total_beats+beat;
            $fwrite(cases_file,"%0d,%06o,%0d,%0d,%0d\n",id,op,MEMORY_MODE<0?waits:0,cycles,beat);
            @(negedge clk); reset=1;tick; // stop lookahead before restoring fixtures
            for(k=0;k<np;k=k+1)poke(patch_a[k],base_word(patch_a[k]));
            for(k=0;k<nt;k=k+1)if(tw[k][0])begin
                if(tw[k][1])poke({ta[k][15:1],1'b0},base_word({ta[k][15:1],1'b0}));
                else poke(ta[k],base_word(ta[k]));
            end
            rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        end
        if(!$feof(f) || checks==0) $fatal(1,"invalid/empty differential fixture");
        if(SUITE==0)for(cls=0;cls<16;cls=cls+1)for(i=0;i<8;i=i+1)for(k=0;k<8;k=k+1)
            if(((cls>=1 && cls<=6) || cls==14) && !coverage[cls][i][k])$fatal(1,"missing mode pair %0d %0d %0d",cls,i,k);
        if(SUITE==1)for(i=40;i<52;i=i+1)for(k=0;k<8;k=k+1)
            if(!single_coverage[i][k])$fatal(1,"missing single operation/mode %0d/%0d",i,k);
        if(SUITE==2)for(i=1;i<16;i=i+1)for(k=0;k<16;k=k+1)
            if(!branch_coverage[i][k])$fatal(1,"missing branch/flags %0d/%0d",i,k);
        if(SUITE==3 || SUITE==4)for(cls=9;cls<=(SUITE==3?9:13);cls=cls+1)
            for(i=0;i<8;i=i+1)for(k=0;k<8;k=k+1)
                if(!coverage[cls][i][k])$fatal(1,"missing byte mode pair %0d %0d %0d",cls,i,k);
        if(SUITE==5)for(i=40;i<52;i=i+1)for(k=0;k<8;k=k+1)
            if(!single_coverage[i][k])$fatal(1,"missing single byte mode %0d %0d",i,k);
        if(SUITE==6)begin
            for(i=1;i<8;i=i+1)for(k=0;k<8;k=k+1)begin
                if(!jump_coverage[i][k])$fatal(1,"missing JMP mode/register %0d/%0d",i,k);
                for(cls=0;cls<8;cls=cls+1)if(!jsr_coverage[cls][i][k])$fatal(1,"missing JSR link/mode/register %0d/%0d/%0d",cls,i,k);
            end
            for(i=0;i<8;i=i+1)begin
                if(!rts_coverage[i])$fatal(1,"missing RTS register");
                for(k=0;k<64;k=k+1)if(!sob_coverage[i][k])$fatal(1,"missing SOB register/offset");
            end
        end
        if(SUITE==7)begin
            for(k=0;k<8;k=k+1)if(!single_coverage[3][k] || !single_coverage[55][k])$fatal(1,"missing SWAB/SXT mode");
            for(k=0;k<64;k=k+1)if(!mark_coverage[k])$fatal(1,"missing MARK offset");
        end
        if(SUITE==8)begin
            for(i=0;i<5;i=i+1)if(!trap_coverage[i])$fatal(1,"missing trap class");
            for(i=0;i<2;i=i+1)for(k=0;k<256;k=k+1)if(!trap_code_coverage[i][k])$fatal(1,"missing EMT/TRAP code");
            for(i=0;i<8;i=i+1)for(k=0;k<8;k=k+1)if(!rti_ipl_coverage[i][k])$fatal(1,"missing RTI IPL pair");
        end
        $fclose(f);$fclose(cases_file);
        $display("PASS %s differential mode%0d: %0d DCJ11 cases, %0d clocks, %0d exact bus beats",suite_name,MEMORY_MODE,checks,total_cycles,total_beats);
        $finish;
    end
    initial begin #400000000; $fatal(1,"EA timeout"); end
endmodule
