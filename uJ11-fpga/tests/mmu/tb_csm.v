`timescale 1ns/1ps
// CSM integration: real microcode/MMU, distinct supervisor I/D mappings,
// caller-stack selection, RTI, disabled/kernel rejection and a protected stack.
module tb_csm;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1;always #5 clk=~clk;
    wire request,writing,byte_access,ready,error,console_active;
    wire [21:0] address;wire [15:0] wdata,rdata,pc,psw,mmr0,mmr1,mmr2,mmr3,regdata;
    reg [4:0] regno=0;
    reg [15:0] ram[0:1048575];
    reg seen=0;integer delay_count=0,pos,i,checks=0,cycles=0,return_pc,handler_base,stack_base,scenario;
    reg ack=0;reg [15:0] value=0;
    assign ready=ack;assign error=address>=22'h200000;
    assign rdata=value;
    uj11_mmu_cpu dut(.clk(clk),.reset(reset),.halt_button(1'b0),.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(16'b0),
        .mem_request(request),.mem_write(writing),.mem_byte(byte_access),.mem_address(address),.mem_write_data(wdata),
        .mem_ready(ready),.mem_error(error),.mem_read_data(rdata),.console_active(console_active),
        .pc(pc),.psw(psw),.mmr0(mmr0),.mmr1(mmr1),.mmr2(mmr2),.mmr3(mmr3),.debug_register_address(regno),.debug_register_data(regdata));
    always @(posedge clk)begin
        ack<=0;cycles<=cycles+1;
        if(reset || !request)begin seen<=0;delay_count<=0;end
        else if(!seen)begin
            if(delay_count==2)begin
                seen<=1;ack<=1;
                if(!error)begin
                    value<=ram[address>>1];
                    if(writing)ram[address>>1]<=wdata;
                end
            end else delay_count<=delay_count+1;
        end
        if(cycles>100000)$fatal(1,"CSM timeout scenario=%0d pc=%o uPC=%h",scenario,pc,dut.upc);
    end
    task check(input bit yes,input string why);if(!yes)$fatal(1,"CSM case%0d: %s pc=%o psw=%o",scenario,why,pc,psw);checks++;endtask
    task word(input [15:0] v);ram[pos>>1]=v;pos+=2;endtask
    task mov(input [15:0] v,input [15:0] a);word('o12737);word(v);word(a);endtask
    task regcheck(input [4:0] r,input [15:0] v,input string why);regno=r;#1;check(regdata==v,why);endtask
    initial begin
        for(i=0;i<1048576;i++)ram[i]=0;
        for(scenario=0;scenario<6;scenario++)begin
            reset=1;repeat(4)@(negedge clk);
            pos='o4000;
            word('o12706);word('o2000);
            mov('o40000,'o177776);word('o12706);word('o2600);mov(0,'o177776);
            handler_base=scenario>=3 && scenario<=4 ? 'o1000000 : 0;
            stack_base=scenario>=3 && scenario<=4 ? 'o2000000 : 0;
            if(handler_base)begin
                mov('o177406,'o172300);mov(0,'o172340);
                mov('o177406,'o172316);mov('o177600,'o172356);
                mov('o177406,'o177600);mov(0,'o177640);
                mov('o177406,'o172200);mov('o10000,'o172240);
                mov(scenario==4 ? 'o177402 : 'o177406,'o172220);mov('o20000,'o172260);
            end
            mov(scenario==0 ? 0 : handler_base ? 'o32 : 'o10,'o172516);
            if(handler_base)mov(1,'o177572);
            if(scenario!=1)mov(scenario==5 ? 'o40000 : 'o140000,'o177776);
            word('o12706);word('o3000);word('o12700);word('o1001);word('o12702);word('o12345);word('o277);
            word(scenario<2 ? 'o7020 : scenario==2 ? 'o7002 : 'o7027);
            if(scenario>2)word('o12345);
            return_pc=pos;word('o12704);word('o1234);word('o104000);
            ram['o10/2]='o7000;ram['o12/2]='o340;
            ram[(handler_base+'o10)/2]='o6000;
            if(scenario<2)ram['o10/2]='o7000;
            ram['o30/2]='o7000;ram['o32/2]='o340;
            ram['o250/2]='o7000;ram['o252/2]='o340;ram['o7000/2]=0;
            pos=handler_base+'o6000;word('o10603);word('o5726);word(2); // MOV SP,R3; TST (SP)+; RTI
            for(i='o2772;i<'o3000;i+=2)ram[(stack_base+i)/2]='o76543;
            reset=0;
            if(scenario>=2 && scenario!=4)begin
                wait(pc=='o6000 && psw[15:14]==1);
                check(psw==(scenario==5 ? 'o50017 : 'o70017),"mode, previous mode and live condition codes");
            end
            wait(console_active);@(negedge clk);
            if(scenario<2)regcheck(0,'o1001,"rejected CSM does not evaluate autoincrement operand");
            else if(scenario==4)begin
                check(mmr0[15:13]==1,"protected supervisor D stack aborts");
                regcheck(17,'o2600,"abort preserves supervisor SP");regcheck(19,'o3000,"abort preserves user SP");
                check(ram[(stack_base+'o2776)/2]=='o76543,"aborted stack store has no side effect");
            end else begin
                regcheck(3,'o2772,"supervisor frame uses caller SP, not old supervisor SP");
                regcheck(4,'o1234,"RTI resumes after immediate operand");
                check(ram[(stack_base+'o2772)/2]=='o12345,"frame operand in supervisor D space");
                check(ram[(stack_base+'o2774)/2]==return_pc,"frame return PC");
                check(ram[(stack_base+'o2776)/2]==(scenario==5?'o40000:'o140000),"stacked PSW clears CC");
                regcheck(17,'o3000,"supervisor SP after popping frame");
                if(scenario!=5)regcheck(19,'o3000,"caller SP preserved");
            end
        end
        $display("PASS MMU CSM: %0d checks",checks);$finish;
    end
endmodule
