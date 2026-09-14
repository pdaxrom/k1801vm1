`timescale 1ns/1ps
module tb_fp_paths_cp75 #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,stopped;
    wire [9:0] upc;
    reg [15:0] memory[0:65535];reg [31:0] v[0:128];
    integer clocks=0,wait_count=0,delay_cycles=0,checks=0,cases=0,manual_cases=0,write_count=0;
    integer vecfile,rc,metrics,start_clocks,start_beats,beats=0;
    reg reread_done=0,failed=0;
    reg [15:0] dirty[0:31];integer ndirty=0;
    reg [131071:0] measured=0;integer measure_index;
    string vector_path,metric_path;
    wire ack=request && wait_count==delay_cycles;
    wire error=ack && !bank && reread_done && !failed && address==v[14] &&
        ((v[13]==1 && !writing) || (v[13]==2 && writing));
    assign rdata=memory[{bank,address[15:1]}];
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.halt_button(1'b0),.debug_block(1'b0),
        .irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(15'b0),.irq_ack(),
        .waiting(),.peripheral_reset(),.mem_addr(address),.mem_write_data(wdata),
        .mem_request(request),.mem_read(),.mem_write(writing),.mem_byte(byte_access),
        .mem_bank(bank),.mem_physical(physical),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(),.retire(),
        .debug_upc(upc),.debug_uword(),.ir(ir),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    `include "fp75_symbols.vh"
    always #5 clk=~clk;
    task eq(input [15:0] got,want,input string what);
        begin
            checks++;
            if(got!==want)$fatal(1,"case%0d op%o kind%0d addr%o %s got%o want%o PC%o IR%o uPC%h",v[79],v[0],v[13],v[14],what,got,want,dut.engine.dp.rf.words[7],ir,upc);
        end
    endtask
    always @(posedge clk) if(reset)wait_count<=0;else begin
        clocks<=clocks+1;
        if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
        if(ack)begin
            beats<=beats+1;
            if(!bank && !writing && dut.engine.service_mode && !reread_done)begin
                eq(address,16'o1000,"first USER read in FP is opcode reread");reread_done<=1;
            end
            if(error)failed<=1;
            if(writing && !error)begin
                if(byte_access)$fatal(1,"unexpected byte write");
                memory[{bank,address[15:1]}]<=wdata;
                if(!bank)begin
                    if(write_count>=v[60])$fatal(1,"unexpected USER write %o=%o case%0d",address,wdata,v[79]);
                    eq(address,v[61+2*write_count],"oracle write address");
                    eq(wdata,v[62+2*write_count],"oracle write data");
                    dirty[ndirty]=address;ndirty++;write_count++;
                end
            end
        end
        if(clocks>200000 || stopped)$fatal(1,"stopped/timeout case%0d PC%o IR%o uPC%h",v[79],dut.engine.dp.rf.words[7],ir,upc);
    end
    initial begin
        for(integer i=0;i<65536;i++)memory[i]=0;
        $readmemh("build/cp75-fp11/test-image.mem",memory);
        if(!$value$plusargs("VECTORS=%s",vector_path))$fatal(1,"VECTORS missing");
        if(!$value$plusargs("METRICS=%s",metric_path))$fatal(1,"METRICS missing");
        vecfile=$fopen(vector_path,"r");metrics=$fopen(metric_path,"w");
        $fwrite(metrics,"opcode,initial_fps,ack_wait_clocks,core_clocks,memory_beats\n");
        while(!$feof(vecfile))begin
            rc=$fscanf(vecfile,"%h",v[0]);
            if(rc==1)begin
                for(integer i=1;i<129;i++)begin rc=$fscanf(vecfile,"%h",v[i]);if(rc!=1)$fatal(1,"incomplete vector");end
                @(negedge clk);reset=1;repeat(4)@(negedge clk);
                clocks=0;beats=0;write_count=0;reread_done=0;failed=0;
                delay_cycles=(cases/2)%4;
                for(integer i=0;i<ndirty;i++)memory[dirty[i]>>1]=0;
                ndirty=0;
                for(integer i=0;i<v[15];i++)begin
                    memory[v[16+2*i]>>1]=v[17+2*i];dirty[ndirty]=v[16+2*i];ndirty++;
                end
                memory[32768]=16'o2400;memory[32769]=16'o340;
                memory[32770]=16'o312;memory[32771]=16'o340;
                memory[32768+16'o6000/2]=v[1];memory[32768+16'o6002/2]=v[2];
                memory[32768+16'o6010/2]=v[3];memory[32768+16'o6012/2]=v[4];
                for(integer i=0;i<7;i++)memory[32768+16'o6100/2+i]=v[5+i];
                @(negedge clk);reset=0;wait(!dut.engine.service_mode);@(negedge clk);
                eq(memory[32768+16'o6004/2],0,"cold initializer");
                eq(dut.engine.service_ready,3,"ODT and FP ready");
                eq(dut.engine.debug_enabled,1,"debug enabled");
                for(integer i=0;i<24;i++)begin
                    eq(memory[32768+FP_ACS/2+i],0,"cold AC zero");memory[32768+FP_ACS/2+i]=v[80+i];
                end
                start_clocks=clocks;start_beats=beats;
                wait(dut.engine.service_mode);wait(!dut.engine.service_mode);@(negedge clk);
                eq(memory[32768+FP_FPS/2],v[48],"oracle FPS");eq(psw,v[49],"oracle PSW");
                eq(memory[32768+FP_FEC/2],v[50],"oracle FEC");eq(memory[32768+FP_FEA/2],v[51],"oracle FEA");
                for(integer i=0;i<8;i++)eq(dut.engine.dp.rf.words[i],v[52+i],$sformatf("oracle R%0d",i));
                for(integer i=0;i<24;i++)eq(memory[32768+FP_ACS/2+i],v[104+i],"expected AC");
                eq(memory[32770],16'o312,"fault vector restored");eq(memory[32771],16'o340,"fault PSW restored");
                eq(write_count,v[60],"all oracle writes observed");eq(failed,v[77],"injection consumed");
                measure_index=v[0]+((v[1]&16'o200)?65536:0);
                if(!measured[measure_index] && !failed && delay_cycles==0)begin
                    measured[measure_index]=1;$fwrite(metrics,"%06o,%06o,0,%0d,%0d\n",v[0],v[1],clocks-start_clocks,beats-start_beats);
                end
                cases++;if(v[128])manual_cases++;
            end else if(!$feof(vecfile))$fatal(1,"bad vector");
        end
        $display("PASS CP75 paths: %0d cases / %0d checks / %0d manual DEC cases",cases,checks,manual_cases);
        $fclose(metrics);$fclose(vecfile);$finish;
    end
endmodule
