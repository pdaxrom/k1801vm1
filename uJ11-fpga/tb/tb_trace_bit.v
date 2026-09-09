`timescale 1ns/1ps
module tb_trace_bit #(parameter integer ROM_DECODE=0, MEMORY_MODE=-1, parameter integer SYSTEM_FLAGS=0, parameter integer PSW_TRANSFER=0, parameter integer SYSTEM_CONTROL=0, parameter integer EIS_ASHC=0, parameter integer EIS_ASH=0, parameter integer EIS_XOR=0, parameter integer EIS_MUL=0, parameter integer EIS_DIV=0);
    reg clk=0,reset=1,irq_valid=0;
    reg [15:0] irq_request=0;
    wire irq_ack,waiting,peripheral_reset;
    integer expect_resets=0,reset_seen=0,clear_irq_on_reset=0;
    integer expect_irq=0,irq_seen=0,retired=0,expect_retired=1,expect_wait=0;
    wire finished=(retired+(retire?1:0)==expect_retired) &&
                  ((expect_wait && !expect_irq) ? waiting : (upc==10'h020 && dut.engine.irq_active==0 && !dut.engine.frame_active));
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault; wire [3:0] rf_address; wire [9:0] upc; wire [35:0] uword;
    reg [7:0] waits=0;
    reg [15:0] pre[0:7],post[0:7],patch_a[0:15],patch_v[0:15];
    reg [15:0] ta[0:31],td[0:31],tw[0:31];
    reg [15:0] op,prepsw,postpsw,value;
    integer f,rc,id,np,nt,i,k,cycles,beat,checks=0,total_cycles=0,total_beats=0,active=0;
    integer cases_file;
    string suite;
    wire prefetch_enable;
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_core #(.ROM_DECODE(ROM_DECODE)) dut(.irq_valid(irq_valid),.irq_priority(irq_request[11:9]),.irq_vector(irq_request[8:1]),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
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
        if(reset)begin irq_seen=0;retired=0;reset_seen=0;irq_valid<=0;end
        else begin
            if(retire)retired=retired+1;
            if(peripheral_reset)begin
                if(!SYSTEM_CONTROL || !active || upc!==10'h022 || request || irq_ack || rf_write || dut.engine.status.update!=0)$fatal(1,"unexpected peripheral reset side effect");
                reset_seen=reset_seen+1;
                if(clear_irq_on_reset)irq_valid<=0;
            end
            if(irq_ack)begin
                if(!active || irq_seen!=0)$fatal(1,"duplicate/unexpected IRQ ack");
                irq_seen=irq_seen+1;irq_valid<=0;
            end
        end
    end
    initial begin
        #100;
        if(EIS_DIV)suite="eis_div";else if(EIS_MUL)suite="eis_mul";else if(EIS_XOR)suite="eis_xor";else if(EIS_ASHC)suite="eis_ashc";else if(EIS_ASH)suite="eis_ash";else if(SYSTEM_CONTROL)suite="system_control";else if(PSW_TRANSFER)suite="psw_transfer";else if(SYSTEM_FLAGS)suite="system_flags";else suite="trace_bit";
        f=$fopen($sformatf("build/%s-vectors.txt",suite),"r");if(!f)$fatal(1,"missing vectors");
        cases_file=$fopen($sformatf("build/%s-cycles-%0d.csv",suite,MEMORY_MODE),"w");
        $fwrite(cases_file,"case,opcode,wait_clocks,microclocks,memory_beats\n");
        for(i=0;i<65536;i=i+2)poke(i[15:0],base_word(i[15:0]));
        rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        while(rc==3) begin
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",pre[k]);
            rc=$fscanf(f,"%h",np);if(np>16)$fatal(1,"patch overflow");
            rc=$fscanf(f,"%h %h %h %h",irq_request,expect_irq,expect_retired,expect_wait);
            if(SYSTEM_CONTROL)rc=$fscanf(f,"%h %h",expect_resets,clear_irq_on_reset);
            for(k=0;k<np;k=k+1)rc=$fscanf(f,"%h %h",patch_a[k],patch_v[k]);
            rc=$fscanf(f,"%h",postpsw);
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",post[k]);
            rc=$fscanf(f,"%h",nt);if(nt>32)$fatal(1,"trace overflow");
            for(k=0;k<nt;k=k+1)rc=$fscanf(f,"%h %h %h",tw[k],ta[k],td[k]);
            @(negedge clk);reset=1;active=0;tick;@(negedge clk);reset=0;
            repeat(17)tick;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=pre[k];
            dut.engine.status.psw=prepsw;
            for(k=0;k<np;k=k+1)poke(patch_a[k],patch_v[k]);
            beat=0;active=1;cycles=0;waits=id%4;irq_valid=irq_request!=0;
            while(!finished && !stopped && cycles<4000)begin tick;cycles=cycles+1;end
            if(!finished || stopped || fault || beat!=nt || psw!==postpsw || reset_seen!=expect_resets || irq_seen!=expect_irq || waiting!=((expect_wait!=0) && !expect_irq))
                $fatal(1,"trace case%0d op%o prePSW%h upc%h clocks%0d beats%0d/%0d flags%h/%h fault%h IRQ%0d/%0d retires%0d",id,op,prepsw,upc,cycles,beat,nt,psw,postpsw,fault,irq_seen,expect_irq,retired+(retire?1:0));
            for(k=0;k<8;k=k+1)if(dut.engine.dp.rf.words[k]!==post[k])
                $fatal(1,"trace case%0d op%o R%0d got%h expected%h",id,op,k,dut.engine.dp.rf.words[k],post[k]);
            active=0;checks=checks+1;total_cycles=total_cycles+cycles;total_beats=total_beats+beat;
            $fwrite(cases_file,"%0d,%06o,%0d,%0d,%0d\n",id,op,MEMORY_MODE<0?waits:0,cycles,beat);
            @(negedge clk);reset=1;tick;
            for(k=0;k<np;k=k+1)poke(patch_a[k],base_word(patch_a[k]));
            for(k=0;k<nt;k=k+1)if(tw[k][0])begin
                if(tw[k][1])poke({ta[k][15:1],1'b0},base_word({ta[k][15:1],1'b0}));
                else poke(ta[k],base_word(ta[k]));
            end
            rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        end
        if(!$feof(f) || checks==0)$fatal(1,"invalid trace fixture");
        $fclose(f);$fclose(cases_file);
        $display("PASS %s differential mode%0d: %0d DCJ11 cases, %0d clocks, %0d exact bus beats",suite,MEMORY_MODE,checks,total_cycles,total_beats);
        $finish;
    end
    initial begin #2000000000; $fatal(1,"trace test timeout");end
endmodule
