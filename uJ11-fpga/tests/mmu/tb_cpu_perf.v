`timescale 1ns/1ps
// Fixed guest workloads for comparing elapsed CPU cycles at 24 and 50 MHz.
// The external-memory model responds in six clocks for both profiles.
module tb_cpu_perf #(parameter integer CLOCK_HZ=24000000,parameter MMU_ENABLE=0,
    parameter IMAGE="build/hc7000-mmu-hardware/microcode.mem");
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1;always #(500000000.0/CLOCK_HZ) clk=~clk;
    wire request,writing,byte_access,ready,error,waiting;
    wire [21:0] address;wire [15:0] wdata,rdata,pc,psw,regdata;
    reg [4:0] regno=0;reg [15:0] ram[0:1048575];
    reg seen=0,ack=0;integer delay_count=0,cycles=0,fetches=0;
    reg [15:0] value=0;
    assign ready=ack;assign error=address>=22'h200000;assign rdata=value;
    uj11_mmu_cpu #(.MICROCODE(IMAGE)) dut(.clk(clk),.reset(reset),.halt_button(1'b0),.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(16'b0),
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
            if(delay_count==5)begin
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
    integer i,pos,n,start_cycles,guest_cycles,loop_pc;
    reg measuring=0;
    integer prep=0,execute=0,memwait=0,translation=0,buswait=0,handshake=0,guest=0;
    always @(posedge clk)if(measuring && !reset)begin
        if(!dut.operands_ready)prep++;
        else if(dut.memory_op && !dut.memory_done)begin
            memwait++;
            if(dut.mmu.state==1 || dut.mmu.state==2 || dut.mmu.state==3)translation++;
            else if(dut.mmu.state==5)buswait++;
            else handshake++;
        end else execute++;
        if(dut.step && dut.fetching)guest++;
    end
    task word(input [15:0] v);begin ram[pos/2]=v;pos=pos+2;end endtask
    initial begin
        for(i=0;i<1048576;i++)ram[i]=0;
        for(n=0;n<4;n++)begin
            @(negedge clk);reset=1;repeat(4)@(negedge clk);
            pos='o4000;
            if(MMU_ENABLE)begin
                // Identity-map code and data through a full kernel I page.
                word('o12737);word('o77406);word('o172300);
                word('o12737);word(0);word('o172340);
                word('o12737);word(1);word('o177572);
            end
            word('o12704);word(n==2 ? 128 : 1000);
            word('o12700);word(n==2 ? 37 : 'o12345);
            word('o12701);word('o3210);
            word('o12702);word(n==2 ? 3 : 'o10000);
            ram['o10000/2]=0;
            if(n==3)begin
                word('o12703);word('o14000);
                for(i=0;i<1000;i++)begin
                    ram['o10000/2+i]=16'h5a5a ^ i;
                    ram['o14000/2+i]=0;
                end
            end
            loop_pc=pos;
            if(n==0)begin
                word('o60100);word('o74001);word('o6001);word('o77404);
            end else if(n==1)begin
                word('o60012);word('o5200);word('o74001);word('o77404);
            end else if(n==2)begin
                word('o12700);word(37);word('o70002);word('o71002);word('o72027);word(1);word('o77407);
            end else begin
                // MOV (R2)+,(R3)+; SOB R4: both EAs have side effects.
                word('o12223);word('o77402);
            end
            word(1);reset=0;
            // Preserve the original unmapped comparison. Mapped profiling
            // excludes map/register setup and starts at the loop itself.
            wait(dut.request && dut.fetching && (!MMU_ENABLE || dut.read_a==loop_pc));
            @(negedge clk);start_cycles=cycles;
            prep=0;execute=0;memwait=0;translation=0;buswait=0;handshake=0;guest=0;measuring=1;
            while(!waiting && cycles<500000)@(negedge clk);
            if(!waiting)$fatal(1,"benchmark timeout");
            measuring=0;guest_cycles=cycles-start_cycles;
            if(prep+execute+memwait!=guest_cycles || translation+buswait+handshake!=memwait)
                $fatal(1,"cycle profile partition");
            if(dut.mmr0[0]!==MMU_ENABLE[0])$fatal(1,"MMU benchmark mode");
            $display("PROFILE %0d instructions=%0d prep=%0d execute=%0d memory=%0d translation=%0d bus=%0d handshake=%0d",
                n,guest,prep,execute,memwait,translation,buswait,handshake);
            regno=0;#1;
            if(regdata!==(n==0 ? 16'o121060 : n==1 ? 16'o14315 : n==2 ? 16'd74 : 16'o12345))
                $fatal(1,"R0 result");
            if(n==3)begin
                for(i=0;i<1000;i++)begin
                    if(ram['o14000/2+i]!==(16'h5a5a ^ i))$fatal(1,"copy result at %0d",i);
                end
                regno=2;#1;if(regdata!=16'o10000+2000)$fatal(1,"source pointer");
                regno=3;#1;if(regdata!=16'o14000+2000)$fatal(1,"destination pointer");
                regno=0;#1;
            end
            $write("PERF %0d cycles=%0d r0=%o",n,guest_cycles,regdata);
            regno=1;#1;$write(" r1=%o",regdata);
            regno=4;#1;if(regdata!=0)$fatal(1,"loop count");
            $display(" mem=%o psw=%o",ram['o10000/2],psw);
        end
        $finish;
    end
endmodule
