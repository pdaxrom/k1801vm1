`timescale 1ns/1ps
module tb_fpp;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0;always #5 clk=~clk;
    reg reset=1,halt_button=0,irq_valid=0;
    reg [2:0] irq_priority=0;reg [15:0] irq_vector=0;
    wire irq_ack,peripheral_reset,mem_request,mem_write,mem_byte,console_active,waiting,retire;
    wire [21:0] mem_address;wire [15:0] mem_write_data;
    reg mem_ready=0,mem_error=0;reg [15:0] mem_read_data=0;
    wire [15:0] psw,ir,pc,mmr0,mmr1,mmr2,mmr3,debug_register_data;
    wire [11:0] upc;wire [53:0] uword;
    reg [4:0] debug_register_address=0;
    uj11_mmu_cpu dut(.*);
    reg [15:0] ram[0:32767];reg [31:0] v[0:128];
    integer cycles=0,checks=0,cases=0,write_count=0,delay_count=0,delay_cycles=0;
    reg seen=0,failed=0,testing=0;
    integer vecfile,rc;string vector_path;
    reg [15:0] dirty[0:63];integer ndirty=0;
    task eq(input [15:0] got,want,input string what);
        begin checks++;if(got!==want)$fatal(1,"case%0d seed%0d op%o kind%0d addr%o %s got%o want%o PC%o IR%o uPC%h",v[79],v[78],v[0],v[13],v[14],what,got,want,pc,ir,upc);end
    endtask
    always @(posedge clk)begin
        cycles<=cycles+1;mem_ready<=0;
        if(reset || !mem_request)begin seen<=0;delay_count<=0;end
        else if(!seen)begin
            if(delay_count==delay_cycles)begin
                seen<=1;mem_ready<=1;
                mem_read_data<=ram[mem_address[15:1]];
                if(testing && !failed && mem_address[15:0]==v[14] &&
                    ((v[13]==1 && !mem_write) || (v[13]==2 && mem_write)))begin mem_error<=1;failed<=1;end
                else begin
                    mem_error<=0;
                    if(mem_write)begin
                        if(mem_byte)$fatal(1,"FP unexpected byte write");
                        if(write_count>=v[60])$fatal(1,"unexpected write %o=%o case%0d",mem_address,mem_write_data,v[79]);
                        eq(mem_address[15:0],v[61+2*write_count],"write address");
                        eq(mem_write_data,v[62+2*write_count],"write data");
                        ram[mem_address[15:1]]<=mem_write_data;
                        dirty[ndirty]=mem_address[15:0];ndirty++;write_count++;
                    end
                end
            end else delay_count<=delay_count+1;
        end
        if(cycles>30000)$fatal(1,"timeout case%0d PC%o IR%o uPC%h",v[79],pc,ir,upc);
    end
    task fpwrite(input integer a,input [15:0] val);
        begin dut.fp_state.storage.low[a]=val[7:0];dut.fp_state.storage.high[a]=val[15:8];end
    endtask
    function [15:0] fpread(input integer a);
        fpread={dut.fp_state.storage.high[a],dut.fp_state.storage.low[a]};
    endfunction
    initial begin
        for(integer i=0;i<32768;i++)ram[i]=0;
        if(!$value$plusargs("VECTORS=%s",vector_path))$fatal(1,"VECTORS missing");
        vecfile=$fopen(vector_path,"r");if(!vecfile)$fatal(1,"cannot open vectors");
        while(!$feof(vecfile))begin
            rc=$fscanf(vecfile,"%h",v[0]);
            if(rc==1)begin
                for(integer i=1;i<129;i++)begin rc=$fscanf(vecfile,"%h",v[i]);if(rc!=1)$fatal(1,"incomplete vector");end
                @(negedge clk);reset=1;testing=0;repeat(4)@(negedge clk);
                cycles=0;write_count=0;failed=0;delay_cycles=cases%4;
                for(integer i=0;i<ndirty;i++)ram[dirty[i]>>1]=0;
                ndirty=0;
                for(integer i=0;i<v[15];i++)begin ram[v[16+2*i]>>1]=v[17+2*i];dirty[ndirty]=v[16+2*i];ndirty++;end
                reset=0;
                while(upc!=12'h020)@(negedge clk);
                dut.psw=v[2];
                for(integer i=0;i<8;i++)dut.dp.rf.words[i==6 ? 16 : i]=v[5+i];
                fpwrite(0,v[1]);fpwrite(1,v[3]);fpwrite(2,v[4]);
                for(integer i=0;i<24;i++)fpwrite(4+i,v[80+i]);
                while(upc==12'h020)@(negedge clk);
                testing=1;
                while(upc!=12'h020)@(negedge clk);
                eq(fpread(0),v[48],"FPS");eq(psw,v[49],"PSW");
                eq(fpread(1),v[50],"FEC");eq(fpread(2),v[51],"FEA");
                for(integer i=0;i<8;i++)eq(dut.dp.rf.words[i==6 ? 16 : i],v[52+i],$sformatf("R%0d",i));
                for(integer i=0;i<24;i++)eq(fpread(4+i),v[104+i],$sformatf("AC word %0d",i));
                eq(write_count,v[60],"write count");eq(failed,v[77],"injection consumed");
                cases++;
            end else if(!$feof(vecfile))$fatal(1,"bad vector");
        end
        $display("PASS MMU FPP: %0d cases, %0d checks",cases,checks);$finish;
    end
endmodule
