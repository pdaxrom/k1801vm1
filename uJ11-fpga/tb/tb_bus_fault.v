`timescale 1ns/1ps
// Fault injection is a logical bus slave in front of RAM/FRAM. An injected
// failed write never reaches the slave; SPI FRAM itself has no error response.
module tb_bus_fault #(parameter integer ROM_DECODE=0, MEMORY_MODE=-1, parameter integer PSW_TRANSFER=0, parameter integer EIS_ASHC=0, parameter integer EIS_ASH=0, parameter integer EIS_XOR=0, parameter integer EIS_MUL=0, parameter integer EIS_DIV=0);
    reg clk=0,reset=1,active=0;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,error,stopped,retire,rf_write,stream;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    reg [15:0] pre[0:7],post[0:7],patch_a[0:7],patch_v[0:7];
    reg [15:0] ta[0:63],td[0:63],tw[0:63];
    reg [15:0] op,prepsw,postpsw,fail_at;
    integer f,rc,id,np,nt,i,k,cycles,beat=0,checks=0,total_cycles=0,total_beats=0;
    integer faults_seen=0,repairs_seen=0,total_repairs=0,retired=0,age=0,waits=0,out;
    string suite,display_suite;
    reg held=0;reg [34:0] held_bus;
    wire injected=active && fail_at!=16'hffff && beat==fail_at;
    wire prefetch_enable,slave_ack;wire [15:0] slave_data;
    wire finished=faults_seen==1 && upc==10'h020 && !dut.engine.irq_active;
    assign ack=request && (injected ? age>=waits : slave_ack);
    assign error=injected;
    assign rdata=injected ? 16'b0 : slave_data;
    uj11_core #(.ROM_DECODE(ROM_DECODE)) dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),
        .clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
        .mem_read(reading),.mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),
        .debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    uj11_prefetch_control policy(.clk(clk),.reset(reset),.uword(uword),.allow(prefetch_enable));
    uj11_stream hint(.uword(uword),.ir(ir),.stream(stream));
    generate if(MEMORY_MODE<0)begin:ram_mode
        reg [7:0] bytes[0:65535];
        assign slave_ack=age>=waits;
        assign slave_data=byte_access?{8'b0,bytes[addr]}:{bytes[addr+16'd1],bytes[addr]};
        always @(posedge clk)if(active && request && ack && writing && !error)begin
            bytes[addr]<=wdata[7:0];if(!byte_access)bytes[addr+16'd1]<=wdata[15:8];
        end
    end else begin:fram_mode
        wire cs_n,sck,mosi,miso,io_request,slave_error;
        uj11_prefetch memory(.prefetch_enable(prefetch_enable),.clk(clk),.reset(reset),
            .request(request && !injected),.write(writing),.byte_access(byte_access),
            .stream(stream),.address(addr),.wdata(wdata),.rdata(slave_data),.ack(slave_ack),.error(slave_error),
            .pc_write(rf_write && rf_address==4'd7),.pc_data(rf_data),.stopped(stopped),
            .io_request(io_request),.io_write(),.io_byte(),.io_address(),.io_wdata(),
            .io_rdata(16'b0),.io_ack(1'b0),.io_error(1'b0),
            .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
        spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
        always @(posedge clk)if(active && (io_request || slave_error))$fatal(1,"fault fixture outside RAM profile");
    end endgenerate
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    function [15:0] base_word;
        input [15:0] a;begin base_word=16'h8000|((a^(a>>3)^16'h3456)&16'h1ffe);end
    endfunction
    task poke;
        input [15:0] a,v;
        begin
            if(MEMORY_MODE<0)begin ram_mode.bytes[a]=v[7:0];ram_mode.bytes[a+16'd1]=v[15:8];end
            else begin fram_mode.fram.memory[a]=v[7:0];fram_mode.fram.memory[a+16'd1]=v[15:8];end
        end
    endtask
    task restore;
        input [15:0] a;reg [15:0] v;reg [7:0] b;
        begin
            v=base_word({a[15:1],1'b0});b=a[0]?v[15:8]:v[7:0];
            if(MEMORY_MODE<0)ram_mode.bytes[a]=b;else fram_mode.fram.memory[a]=b;
        end
    endtask
    task tick;begin @(posedge clk);#1;end endtask
    always @(posedge clk)begin
        if(reset)begin age<=0;held<=0;faults_seen=0;repairs_seen=0;retired=0;end
        else if(active)begin
            if(held && (!request || held_bus!=={reading,writing,byte_access,addr,wdata}))$fatal(1,"fault request changed while waiting");
            held<=request&&!ack;held_bus<={reading,writing,byte_access,addr,wdata};
            if(!request || ack)age<=0;else age<=age+1;
            if(retire)retired=retired+1;
            if(dut.engine.fault_redirect)begin
                faults_seen=faults_seen+1;
                if(dut.engine.step || rf_write)$fatal(1,"failed operation committed");
            end
            if(dut.engine.fault_repair)repairs_seen=repairs_seen+1;
            if(request && ack)begin
                if(beat>=nt || addr!==ta[beat] || writing!==tw[beat][0] || byte_access!==tw[beat][1] ||
                   error!==tw[beat][2] || (byte_access ? (writing ? wdata[7:0]!==td[beat][7:0] : rdata[7:0]!==td[beat][7:0]) : (writing ? wdata!==td[beat] : rdata!==td[beat])))
                    $fatal(1,"fault case%0d op%o beat%0d got %b%b%b %h:%h want %h %h:%h upc%h",id,op,beat,
                           error,byte_access,writing,addr,writing?wdata:rdata,tw[beat],ta[beat],td[beat],upc);
                beat=beat+1;
            end
        end
    end
    initial begin
        #100;
        if(EIS_DIV)begin suite="eis-div-fault";display_suite="DIV fault";end
        else if(EIS_MUL)begin suite="eis-mul-fault";display_suite="MUL fault";end
        else if(EIS_XOR)begin suite="eis-xor-fault";display_suite="XOR fault";end
        else if(EIS_ASHC)begin suite="eis-ashc-fault";display_suite="ASHC fault";end
        else if(EIS_ASH)begin suite="eis-ash-fault";display_suite="ASH fault";end
        else if(PSW_TRANSFER)begin suite="psw-transfer-fault";display_suite="PSW-transfer fault";end
        else begin suite="bus-fault";display_suite="bus fault";end
        f=$fopen($sformatf("build/%s-vectors.txt",suite),"r");if(!f)$fatal(1,"missing fault fixture");
        out=$fopen($sformatf("build/%s-cycles-%0d.csv",suite,MEMORY_MODE),"w");
        $fwrite(out,"case,opcode,wait_clocks,microclocks,memory_beats,repair_clocks\n");
        for(i=0;i<65536;i=i+2)poke(i[15:0],base_word(i[15:0]));
        rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        while(rc==3)begin
            for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",pre[k]);
            rc=$fscanf(f,"%h %h",fail_at,np);if(np>8)$fatal(1,"fault patch overflow");
            for(k=0;k<np;k=k+1)rc=$fscanf(f,"%h %h",patch_a[k],patch_v[k]);
            rc=$fscanf(f,"%h",postpsw);for(k=0;k<8;k=k+1)rc=$fscanf(f,"%h",post[k]);
            rc=$fscanf(f,"%h",nt);if(nt>64)$fatal(1,"fault trace overflow");
            for(k=0;k<nt;k=k+1)rc=$fscanf(f,"%h %h %h",tw[k],ta[k],td[k]);
            @(negedge clk);reset=1;active=0;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            for(k=0;k<8;k=k+1)dut.engine.dp.rf.words[k]=pre[k];
            dut.engine.status.psw=prepsw;
            for(k=0;k<np;k=k+1)poke(patch_a[k],patch_v[k]);
            active=1;beat=0;cycles=0;waits=id%4;
            while(!finished && !stopped && cycles<2500)begin tick;cycles=cycles+1;end
            if(!finished || stopped || fault || beat!=nt || psw!==postpsw || retire || retired || dut.engine.frame_active || dut.engine.seq.link_valid)
                $fatal(1,"fault case%0d op%o outcome upc%h cycles%0d beat%0d/%0d psw%h/%h stopped%b",id,op,upc,cycles,beat,nt,psw,postpsw,stopped);
            for(k=0;k<8;k=k+1)if(dut.engine.dp.rf.words[k]!==post[k])$fatal(1,"fault case%0d op%o R%0d got%h expected%h",id,op,k,dut.engine.dp.rf.words[k],post[k]);
            if(faults_seen!=1 || repairs_seen>1)$fatal(1,"fault redirect count");
            total_cycles=total_cycles+cycles;total_beats=total_beats+beat;total_repairs=total_repairs+repairs_seen;checks=checks+1;
            $fwrite(out,"%0d,%06o,%0d,%0d,%0d,%0d\n",id,op,waits,cycles,beat,repairs_seen);
            @(negedge clk);active=0;reset=1;tick;
            for(k=0;k<np;k=k+1)begin restore(patch_a[k]);restore(patch_a[k]+16'd1);end
            for(k=0;k<nt;k=k+1)if(tw[k][0])begin restore(ta[k]);restore(ta[k]+16'd1);end
            rc=$fscanf(f,"%h %h %h",id,op,prepsw);
        end
        if(!$feof(f) || checks==0)$fatal(1,"empty/malformed fault fixture");
        $fclose(f);$fclose(out);
        $display("PASS %s differential mode%0d: %0d DCJ11 vector-boundary cases, %0d clocks, %0d exact bus beats, %0d repair clocks",display_suite,MEMORY_MODE,checks,total_cycles,total_beats,total_repairs);$finish;
    end
    initial begin #2000000000;$fatal(1,"bus fault differential timeout");end
endmodule
