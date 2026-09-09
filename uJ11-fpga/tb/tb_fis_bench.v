`timescale 1ns/1ps
module tb_fis_bench #(parameter integer MEMORY_MODE=-1, CANDIDATE_ROM=0);
    reg clk=0,reset=1,active=0;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write;
    wire irq_ack,waiting,stream,prefetch_enable;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    wire [15:0] raw_data;wire raw_ack,raw_error;
    wire physical_request=request,irq_valid=1'b0;
    assign ack=raw_ack;assign error=raw_error;assign rdata=raw_data;
    reg [7:0] waits=0;
    integer spi_clocks=0,spi_transactions=0,beats=0;
    integer file,fixture,rc,w=0,k,n,clocks,last_retire,fis_clocks,fis_count;
    reg [15:0] operation,expected_flags;
    reg [31:0] arg_a,arg_b,answer;
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
    always @(posedge clk)if(active && request && ack)beats=beats+1;
    initial begin
        #100;
`ifdef FIS_FRAM
        if(MEMORY_MODE<0)$fatal(1,"FIS_FRAM requires FRAM mode");
`else
        if(MEMORY_MODE>=0)$fatal(1,"FRAM mode requires FIS_FRAM");
`endif
        file=$fopen($sformatf("build/fis-benchmarks-%0d.json",MEMORY_MODE),"w");
        fixture=$fopen("build/fis-bench-vectors.txt","r");
        if(file==0 || fixture==0)$fatal(1,"FIS benchmark file open failed");
        $fwrite(file,"[\n");
        rc=$fscanf(fixture,"%h %h %h %h %h",operation,arg_a,arg_b,answer,expected_flags);
        while(rc==5)begin
            @(negedge clk);active=0;reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=16'h1200+16'(k);
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.dp.rf.words[7]=0;
            dut.engine.status.psw=16'h00e0;
            // Five instructions per loop. Refresh A and R0 using guest MOVs.
            poke(0,16'o012737);poke(2,arg_a[31:16]);poke(4,16'h1004);
            poke(6,16'o012737);poke(8,arg_a[15:0]);poke(10,16'h1006);
            poke(12,16'o012700);poke(14,16'h1000);
            poke(16,16'o075000 | (operation<<3));poke(18,16'o000766);
            poke(16'h1000,arg_b[31:16]);poke(16'h1002,arg_b[15:0]);
            n=0;clocks=0;
            while(n<5)begin
                tick;clocks=clocks+1;
                if(retire)n=n+1;
                if(stopped || clocks>10000)$fatal(1,"FIS warmup op%0d",operation);
            end
            clocks=0;n=0;last_retire=0;fis_clocks=0;fis_count=0;
            beats=0;spi_clocks=0;spi_transactions=0;active=1;
            while(n<160)begin
                tick;clocks=clocks+1;
                if(stopped || clocks>200000)$fatal(1,"FIS benchmark timeout op%0d",operation);
                if(retire)begin
                    n=n+1;
                    if((ir&16'o177740)==16'o075000)begin
                        fis_count=fis_count+1;fis_clocks=fis_clocks+clocks-last_retire;
                        if(dut.engine.dp.rf.words[0]!==16'h1004 || psw!==(16'he0|expected_flags) ||
                           {peek(16'h1004),peek(16'h1006)}!==answer)
                            $fatal(1,"FIS benchmark result op%0d got%h flags%h expected%h/%h",operation,
                                {peek(16'h1004),peek(16'h1006)},psw,answer,expected_flags);
                    end
                    last_retire=clocks;
                end
            end
            active=0;
            if(fis_count!=32 || dut.engine.dp.rf.words[7]!==0 || dut.engine.dp.rf.words[6]!==16'h6000 ||
               {peek(16'h1000),peek(16'h1002)}!==arg_b || irq_ack || waiting || fault!=0)
                $fatal(1,"FIS benchmark state op%0d",operation);
            for(k=1;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==16'h1200+16'(k))$fatal(1,"FIS clobbered R%0d",k);
            $fwrite(file,"%s  {\"operation\":%0d,\"a\":\"%08h\",\"b\":\"%08h\",\"instructions\":160,\"fis_instructions\":32,\"microclocks\":%0d,\"fis_retire_clocks\":%0d,\"memory_beats\":%0d,\"spi_clocks\":%0d,\"spi_transactions\":%0d}",
                w==0?"":",\n",operation,arg_a,arg_b,clocks,fis_clocks,beats,spi_clocks,spi_transactions);
            $display("FIS BENCH mode%0d op%0d A%h B%h: %0d clocks, FIS %0d, beats%0d",MEMORY_MODE,operation,arg_a,arg_b,clocks,fis_clocks,beats);
            w=w+1;
            rc=$fscanf(fixture,"%h %h %h %h %h",operation,arg_a,arg_b,answer,expected_flags);
        end
        if(w!=8 || (rc!=-1 && !$feof(fixture)))$fatal(1,"FIS benchmark count/format");
        $fwrite(file,"\n]\n");$fclose(file);$fclose(fixture);
        $display("PASS FIS benchmarks mode%0d: 8 workloads, 32 FIS and 160 total instructions each; one warmup loop excluded",MEMORY_MODE);
        $finish;
    end
endmodule
