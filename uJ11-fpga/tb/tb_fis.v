`timescale 1ns/1ps
// Real production core, synchronous ROM and RAM or MR45V100A SPI transport.
// Expectations come from exact rational arithmetic and a separate ISA model.
module tb_fis #(parameter integer MEMORY_MODE=-1, CANDIDATE_ROM=1);
    reg clk=0,reset=1,active=0,irq_valid=0;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write;
    wire irq_ack,waiting,stream,prefetch_enable;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [15:0] raw_data;
    wire raw_ack,raw_error;
    reg [7:0] waits=0;
    integer f,rc,id,cycles,beat,nt,np,nc,i,k,checks=0,irq_seen=0;
    integer fault_beat=-1, want_irq, expect_irq, expect_fault, failed_cases=0, entered=0;
    string vector_file, result_file, rom_file;
    reg [15:0] op,prepsw,postpsw,fault_spec;
    reg [15:0] pre[0:7],post[0:7],pa[0:31],pv[0:31];
    reg [15:0] ta[0:31],td[0:31],tw[0:31],ca[0:31],cv[0:31];
    integer results,spi_clocks=0,spi_transactions=0;
    wire inject_error=active && request && beat==fault_beat;
    wire physical_request=request && !inject_error;
    assign ack=inject_error || raw_ack;
    assign error=inject_error || raw_error;
    assign rdata=inject_error ? 16'b0 : raw_data;
    uj11_core dut(.clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(3'd7),
        .irq_vector(8'o040),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),
        .mem_addr(addr),.mem_write_data(wdata),.mem_request(request),.mem_read(reading),
        .mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),
        .debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data));
    uj11_stream hint(.uword(uword),.ir(ir),.stream(stream));
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    generate if(MEMORY_MODE<0) begin: ram_mode
        wire [31:0] transactions,writes;
        assign raw_error=0;
        uj11_ram ram(.clk(clk),.reset(reset),.request(physical_request),.reading(reading),
            .writing(writing),.byte_access(byte_access),.addr(addr),.write_data(wdata),
            .wait_states(waits),.ack(raw_ack),.read_data(raw_data),
            .transactions(transactions),.writes(writes));
    end else begin: fram_mode
        wire cs_n,sck,mosi,miso;
        uj11_prefetch #(.PREFETCH(MEMORY_MODE==2)) memory(.clk(clk),.reset(reset),
            .request(physical_request),.write(writing),.byte_access(byte_access),
            .stream(stream),.prefetch_enable(prefetch_enable),.address(addr),.wdata(wdata),
            .rdata(raw_data),.ack(raw_ack),.error(raw_error),
            .pc_write(rf_write && rf_address==4'd7),.pc_data(rf_data),.stopped(stopped),
            .io_request(),.io_write(),.io_byte(),.io_address(),.io_wdata(),
            .io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
            .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
        spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
        always @(posedge sck) if(active && !cs_n)spi_clocks=spi_clocks+1;
        always @(negedge cs_n) if(active)spi_transactions=spi_transactions+1;
    end endgenerate
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`else
    initial if(CANDIDATE_ROM!=0)begin
        if(!$value$plusargs("rom=%s",rom_file))rom_file="build/fis.mem";
        #1;$readmemh(rom_file,dut.engine.rom.words);
    end
`endif
    always #5 clk=~clk;
    task tick;begin @(posedge clk);#1;end endtask
    task poke(input [15:0] a,v);
        begin
`ifdef FIS_FRAM
            fram_mode.fram.memory[{1'b0,a}]=v[7:0];fram_mode.fram.memory[{1'b0,16'(a+16'd1)}]=v[15:8];
`else
            ram_mode.ram.bytes[a]=v[7:0];ram_mode.ram.bytes[a+16'd1]=v[15:8];
`endif
        end
    endtask
    function [15:0] peek(input [15:0] a);
        begin
`ifdef FIS_FRAM
            peek={fram_mode.fram.memory[{1'b0,16'(a+16'd1)}],fram_mode.fram.memory[{1'b0,a}]};
`else
            peek={ram_mode.ram.bytes[a+16'd1],ram_mode.ram.bytes[a]};
`endif
        end
    endfunction
    always @(posedge clk)if(active && request && ack)begin
        if(beat>=nt || addr!==ta[beat] || writing!==tw[beat][0] || byte_access || error!==tw[beat][2] ||
           (!error && (writing?wdata:rdata)!==td[beat]))
            $fatal(1,"FIS case%0d op%o beat%0d upc%h got wr%b err%b %h:%h expected %h %h:%h",
                id,op,beat,upc,writing,error,addr,writing?wdata:rdata,tw[beat],ta[beat],td[beat]);
        beat<=beat+1;
    end
    always @(posedge clk)if(active && irq_ack)begin
        irq_seen=irq_seen+1;irq_valid<=0;
    end
    initial begin
        #100;
`ifdef FIS_FRAM
        if(MEMORY_MODE<0)$fatal(1,"FIS_FRAM requires FRAM mode");
`else
        if(MEMORY_MODE>=0)$fatal(1,"FRAM mode requires FIS_FRAM");
`endif
        if(!$value$plusargs("vectors=%s",vector_file))vector_file="build/fis-vectors.txt";
        if(!$value$plusargs("results=%s",result_file))result_file=$sformatf("build/fis-cycles-%0d.csv",MEMORY_MODE);
        f=$fopen(vector_file,"r");if(f==0)$fatal(1,"FIS vectors missing");
        results=$fopen(result_file,"w");if(results==0)$fatal(1,"FIS results open failed");
        $fwrite(results,"case,opcode,microclocks,memory_beats,spi_clocks,spi_transactions\n");
        for(i=0;i<65536;i=i+2)poke(i[15:0],16'ha55a);
        rc=$fscanf(f,"%h %h %h %h %h %h %h",id,op,prepsw,want_irq,expect_irq,fault_spec,expect_fault);
        while(rc==7)begin
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",pre[k]);
            rc=$fscanf(f,"%h",np);if(np>32)$fatal(1,"FIS patches overflow");
            for(k=0;k<np;k=k+1)rc=$fscanf(f,"%h %h",pa[k],pv[k]);
            rc=$fscanf(f,"%h",postpsw);
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",post[k]);
            rc=$fscanf(f,"%h",nt);if(nt>32)$fatal(1,"FIS trace overflow");
            for(k=0;k<nt;k=k+1)rc=$fscanf(f,"%h %h %h",tw[k],ta[k],td[k]);
            rc=$fscanf(f,"%h",nc);if(nc>32)$fatal(1,"FIS memory checks overflow");
            for(k=0;k<nc;k=k+1)rc=$fscanf(f,"%h %h",ca[k],cv[k]);
            if(rc!=2)$fatal(1,"FIS truncated record");
            @(negedge clk);active=0;reset=1;irq_valid=0;tick;
            @(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=pre[k];
            dut.engine.status.psw=prepsw;
            for(k=0;k<np;k=k+1)poke(pa[k],pv[k]);
            beat=0;cycles=0;irq_seen=0;spi_clocks=0;spi_transactions=0;
            fault_beat=fault_spec==16'hffff ? -1 : {16'b0,fault_spec};
            waits={6'b0,id[1:0]};irq_valid=want_irq!=0;active=1;
            entered=0;
            while(!(entered!=0 && upc==10'h020 && dut.engine.irq_active==0) && !stopped && cycles<4000)begin
                tick;cycles=cycles+1;
                if(upc!=10'h020)entered=1;
            end
            if(stopped!==(expect_fault!=0) || fault!==expect_fault[1:0] || cycles>=4000 || beat!=nt || psw!==postpsw || irq_seen!=expect_irq)
                $fatal(1,"FIS case%0d op%o done upc%h cycles%0d beats%0d/%0d psw%h/%h irq%0d/%0d",
                    id,op,upc,cycles,beat,nt,psw,postpsw,irq_seen,expect_irq);
            for(k=0;k<8;k=k+1)if(dut.engine.dp.rf.words[k]!==post[k])
                $fatal(1,"FIS case%0d R%0d got%h expected%h",id,k,dut.engine.dp.rf.words[k],post[k]);
            for(k=0;k<nc;k=k+1)if(peek(ca[k])!==cv[k])
                $fatal(1,"FIS case%0d memory%h got%h expected%h",id,ca[k],peek(ca[k]),cv[k]);
            $fwrite(results,"%0d,%06o,%0d,%0d,%0d,%0d\n",id,op,cycles,beat,spi_clocks,spi_transactions);
            active=0;checks=checks+1;
            if(fault_beat>=0)failed_cases=failed_cases+1;
            rc=$fscanf(f,"%h %h %h %h %h %h %h",id,op,prepsw,want_irq,expect_irq,fault_spec,expect_fault);
        end
        if(checks==0 || (rc!=-1 && !$feof(f)))$fatal(1,"FIS malformed vector header");
        $fclose(f);$fclose(results);
        $display("PASS FIS memory_mode=%0d: %0d exact state/PSW/bus/memory cases, %0d injected faults",MEMORY_MODE,checks,failed_cases);
        $finish;
    end
endmodule
