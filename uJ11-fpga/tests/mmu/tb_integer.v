`timescale 1ns/1ps
// Differential execution of real guest code, including effective-address
// side effects and trap frames, against tests/reference_integer.c.
module tb_integer;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1;always #5 clk=~clk;
    wire request,writing,byte_access,ready,error,waiting;
    wire [21:0] address;wire [15:0] wdata,rdata,pc,psw,regdata;
    reg [4:0] regno=0;reg [15:0] ram[0:1048575];
    reg seen=0,ack=0;integer delay_count=0,cycles=0,fetches=0;
    reg [15:0] value=0;
    assign ready=ack;assign error=address>=22'h200000;assign rdata=value;
    uj11_mmu_cpu dut(.clk(clk),.reset(reset),.halt_button(1'b0),.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(16'b0),
        .mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_address(address),.mem_write_data(wdata),
        .mem_ready(ready),.mem_error(error),.mem_read_data(rdata),.waiting(waiting),
        .pc(pc),.psw(psw),.debug_register_address(regno),.debug_register_data(regdata));
    always @(posedge clk)begin
        ack<=0;
        if(reset)begin cycles<=0;fetches<=0;end
        else begin
            cycles<=cycles+1;
            if(dut.request && !dut.memory_started && dut.fetching)fetches<=fetches+1;
        end
        if(reset || !request)begin seen<=0;delay_count<=0;end
        else if(!seen)begin
            if(delay_count==2)begin
                seen<=1;ack<=1;
                if(!error)begin
                    value<=ram[address>>1];
                    if(writing)begin
                        if(!byte_access || !address[0])ram[address>>1][7:0]<=wdata[7:0];
                        if(!byte_access || address[0])ram[address>>1][15:8]<=wdata[15:8];
                    end
                end
            end else delay_count<=delay_count+1;
        end
    end
    integer fd,rc,id,nwords,nprobes,nins,i,j,a,v,checks=0,failures=0,count=0;
    reg [15:0] expected[0:10];string fixtures,name;
    task check(input [15:0] got,input [15:0] want,input string what);
        checks++;
        if(got!==want)begin
            if(failures<30)$display("FAIL case=%0d %s %s got=%o expected=%o pc=%o",id,name,what,got,want,pc);
            failures++;
        end
    endtask
    initial begin
        if(!$value$plusargs("fixtures=%s",fixtures))$fatal(1,"missing fixtures");
        fd=$fopen(fixtures,"r");if(!fd)$fatal(1,"fixture open failed");
        for(i=0;i<1048576;i++)ram[i]=0;
        while(!$feof(fd))begin
            rc=$fscanf(fd,"%h %h %h %h %s\n",id,nwords,nprobes,nins,name);
            if(rc==5)begin
                reset=1;repeat(4)@(negedge clk);
                // Programs use the low 64 KiB and three split D segments.
                for(i=0;i<32768;i++)ram[i]=0;
                for(j=1;j<=4;j++)for(i=0;i<4096;i++)ram[j*131072+i]=0;
                for(i=0;i<nwords;i++)begin rc=$fscanf(fd,"%h %h\n",a,v);ram[a>>1]=v;end
                for(i=0;i<11;i++)rc=$fscanf(fd,"%h",expected[i]);
                reset=0;
                while(fetches<=nins && cycles<15000)@(negedge clk);
                check(fetches,nins+1,"reaches next instruction");
                for(i=0;i<8;i++)begin
                    regno=i==6 ? 16+psw[15:14] : i<6 ? i+8*psw[11] : i;
                    #1;check(regdata,expected[i],$sformatf("R%0d",i));
                end
                check(psw,expected[8],"PSW");
                check(dut.cpuerr,expected[9],"CPUERR");
                check({dut.pirq_requests,9'b0},expected[10],"PIRQ requests");
                for(i=0;i<nprobes;i++)begin
                    rc=$fscanf(fd,"%h %h\n",a,v);check(ram[a>>1],v,$sformatf("RAM[%o]",a));
                end
                count++;
            end
        end
        $fclose(fd);
        if(failures)$fatal(1,"integer differential: %0d failures / %0d checks, %0d programs",failures,checks,count);
        $display("PASS MMU integer differential: %0d checks, %0d programs",checks,count);$finish;
    end
endmodule
