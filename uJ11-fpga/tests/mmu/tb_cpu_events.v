`timescale 1ns/1ps
// Execute guest instructions against the real CPU/MMU with a stalled bus.
// Fault injection covers errors a RAM-only C fixture cannot generate.
module tb_cpu_events;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1;always #5 clk=~clk;
    reg irq_valid=0;reg [2:0] irq_priority=0;
    wire irq_ack,request,writing,byte_access,waiting,console_active;
    wire [21:0] address;wire [15:0] wdata,pc,psw,regdata;
    reg [4:0] regno=0;
    reg [15:0] ram[0:1048575];
    reg ack=0,error=0,seen=0;reg [15:0] data=0;
    reg inject=0,emergency_error=0;reg [21:0] bad_address=0;
    integer delay_count=0,cycles=0,acks=0,checks=0,pos,i,p,e,k,stop_pc,sp;
    uj11_mmu_cpu dut(.clk(clk),.reset(reset),.halt_button(1'b0),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(16'o120),.irq_ack(irq_ack),
        .mem_request(request),.mem_write(writing),.mem_byte(byte_access),
        .mem_address(address),.mem_write_data(wdata),.mem_ready(ack),.mem_error(error),.mem_read_data(data),
        .waiting(waiting),.console_active(console_active),.pc(pc),.psw(psw),
        .debug_register_address(regno),.debug_register_data(regdata));
    always @(posedge clk)begin
        ack<=0;
        if(reset)begin cycles<=0;acks<=0;end
        else begin
            cycles<=cycles+1;
            if(irq_ack)begin acks<=acks+1;irq_valid<=0;end
        end
        if(reset || !request)begin seen<=0;delay_count<=0;end
        else if(!seen)begin
            if(delay_count==3)begin
                seen<=1;ack<=1;
                error<=address>=22'h200000 || (inject && address==bad_address) || (emergency_error && writing && address==0);
                if(address<22'h200000 && !(inject && address==bad_address) && !(emergency_error && writing && address==0))begin
                    data<=ram[address>>1];
                    if(writing)begin
                        if(!byte_access || !address[0])ram[address>>1][7:0]<=wdata[7:0];
                        if(!byte_access || address[0])ram[address>>1][15:8]<=wdata[15:8];
                    end
                end
                if({address[21:1],1'b0}==22'o17777766 || {address[21:1],1'b0}==22'o17777772)
                    $fatal(1,"internal CPU CSR escaped onto peripheral bus");
            end else delay_count<=delay_count+1;
        end
    end
    task check(input bit ok,input string text);
        checks++;
        if(!ok)$fatal(1,"%s PC=%o PSW=%o uPC=%h CPUERR=%o PIRQ=%o",text,pc,psw,dut.upc,dut.cpuerr,dut.pirq);
    endtask
    task word(input [15:0] v);ram[pos>>1]=v;pos+=2;endtask
    task mov(input [15:0] v,input [15:0] a);word('o12737);word(v);word(a);endtask
    task regset(input integer r,input [15:0] v);word('o12700|r);word(v);endtask
    task prepare;
        @(negedge clk);reset=1;repeat(4)@(negedge clk);
        for(i=0;i<32768;i++)ram[i]=0;
        ram[4/2]='o6000;ram[6/2]='o340;
        ram['o14/2]='o6100;ram['o16/2]='o340;
        ram['o240/2]='o6200;ram['o242/2]='o340;
        ram['o120/2]='o6300;ram['o122/2]='o340;
        ram['o6000/2]=1;ram['o6100/2]=1;ram['o6200/2]=1;ram['o6300/2]=1;
        pos='o4000;inject=0;emergency_error=0;irq_valid=0;irq_priority=0;
        regset(6,'o2000);
    endtask
    task seek(input [15:0] target);
        reset=0;
        while(!(dut.request && dut.fetching && !dut.memory_started && pc==target) && cycles<12000)@(negedge clk);
        check(cycles<12000,$sformatf("reach %o",target));
    endtask
    task mapping;
        mov('o177406,'o172300);mov(0,'o172340);
        mov('o177406,'o172302);mov('o100000,'o172342);
        mov('o177406,'o172316);mov('o177600,'o172356);
        mov('o20,'o172516);mov(1,'o177572);
    endtask
    initial begin
        for(i=0;i<1048576;i++)ram[i]=0;
        // Software IRQ arbitration: higher external levels preempt PIRQ;
        // equal levels choose PIRQ and must not acknowledge the device.
        for(p=1;p<=7;p++)for(e=1;e<=7;e++)begin
            prepare();mov(1<<(p+8),'o177772);mov(0,'o177776);stop_pc=pos;
            irq_priority=e;irq_valid=1;
            seek(p>=e ? 'o6200 : 'o6300);
            check(acks==(e>p),"only selected external interrupt acknowledged");
            check(dut.pirq_requests==(1<<(p-1)),"PIRQ grant retains request until software clears it");
            check(ram['o1774/2]==stop_pc && ram['o1776/2]==0,"IRQ saves post-instruction PC/PSW");
        end
        // RESET clears PIRQ; masked requests must leave WAIT asleep.
        prepare();mov('o177000,'o177772);word(5);stop_pc=pos;word(1);
        seek(stop_pc);check(dut.pirq==0,"RESET clears PIRQ");
        prepare();mov('o1000,'o177772);word(1);reset=0;
        while(!waiting && cycles<12000)@(negedge clk);
        check(waiting && dut.pirq_priority==1 && acks==0,"masked PIRQ leaves WAIT asleep");
        irq_priority=7;irq_valid=1;
        // Still masked at IPL7. No side effect from a pending equal-level IRQ.
        repeat(12)@(negedge clk);check(waiting && acks==0,"WAIT respects equal IPL");
        // A device handler queues PIR1 at IPL7. Its RTT restores IPL0 and
        // takes the software vector before the instruction following WAIT.
        prepare();mov(0,'o177776);word(1);stop_pc=pos;
        pos='o6300;mov('o1000,'o177772);word(6);reset=0;
        while(!waiting && cycles<12000)@(negedge clk);
        check(waiting,"guest entered WAIT before the external request");
        irq_priority=7;irq_valid=1;seek('o6200);
        check(acks==1 && ram['o1774/2]==stop_pc && dut.pirq_requests==1,"RTT from IRQ services queued PIRQ with correct return PC");
        // ADR, I/O timeout, and NXM must have distinct CPUERR causes.
        for(k=0;k<3;k++)begin
            prepare();
            if(k==2)mapping();
            word('o13700);word(k==0 ? 'o1001 : k==1 ? 'o177774 : 'o20000);
            stop_pc=pos;seek('o6000);
            check(dut.cpuerr==(k==0 ? 'o100 : k==1 ? 'o20 : 'o40),"distinct bus/address error cause");
            check(ram['o1774/2]==stop_pc,"operand fault saves advanced PC");
        end
        // Opcode fetches from internal registers raise ADR without invoking
        // the peripheral bus. Ordinary data reads remain legal.
        for(k=0;k<7;k++)begin
            prepare();
            case(k)
                0:stop_pc='o177776;1:stop_pc='o177772;2:stop_pc='o177766;
                3:stop_pc='o177572;4:stop_pc='o172300;5:stop_pc='o172516;6:stop_pc='o177752;
            endcase
            regset(7,stop_pc);seek('o6000);
            check(dut.cpuerr=='o100 && ram['o1774/2]==stop_pc,"internal opcode fetch raises ADR");
        end
        // A real trap-stack bus abort on either push uses physical 0/2,
        // preserves the original frame and finishes with kernel SP=0.
        for(k=0;k<2;k++)begin
            prepare();mov(3,'o177776);word(3);stop_pc=pos;
            inject=1;bad_address=k==0 ? 'o1776 : 'o1774;
            seek('o6000);regno=16;#1;
            check(dut.cpuerr=='o44,"kernel push bus abort sets RED and NXM");
            check(ram[0]==stop_pc && ram[1]==3 && regdata==0,"emergency stack preserves original PC/PSW");
        end
        prepare();mov(3,'o177776);word(3);inject=1;bad_address='o1776;emergency_error=1;reset=0;
        while(!console_active && cycles<12000)@(negedge clk);
        check(console_active && dut.cpuerr=='o44,"failed emergency stack enters ODT without recursive traps");
        $display("PASS MMU CPU events: %0d checks",checks);$finish;
    end
endmodule
