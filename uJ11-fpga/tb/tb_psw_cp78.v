`timescale 1ns/1ps
// Native instruction tests. Initialize external RAM only; no forced CPU state.
module tb_psw_cp78 #(parameter integer ROM_DECODE=1, ALIGNED_WORD_READS=1);
    reg clk=0,reset=1,irq=0;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,stopped,irq_ack;
    reg [15:0] memory[0:65535];
    integer clocks=0,checks=0,cases=0,wait_count=0,delay_cycles=0,psw_beats=0;
    wire ack=request && wait_count==delay_cycles;
    // Even if an external slave spuriously asserts ERROR at PSW, it must
    // not affect the internal transaction. Adjacent unmapped I/O must fault.
    wire error=!physical && address>=16'o160000;
    wire [15:0] word_data=memory[{bank,address[15:1]}];
    assign rdata=ALIGNED_WORD_READS!=0 || !byte_access ? word_data :
                 {8'b0,address[0] ? word_data[15:8] : word_data[7:0]};
    uj11_core #(.ROM_DECODE(ROM_DECODE),.ALIGNED_WORD_READS(ALIGNED_WORD_READS)) dut(
        .clk(clk),.reset(reset),.halt_button(1'b0),.debug_block(1'b0),
        .irq_valid(irq),.irq_priority(3'd7),.irq_vector(8'o100),.irq_ack(irq_ack),
        .waiting(),.peripheral_reset(),.mem_addr(address),.mem_write_data(wdata),
        .mem_request(request),.mem_read(),.mem_write(writing),.mem_byte(byte_access),
        .mem_bank(bank),.mem_physical(physical),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(),.retire(),
        .debug_upc(),.debug_uword(),.ir(ir),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk) if(reset)wait_count<=0;else begin
        clocks<=clocks+1;
        if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
        if(irq_ack)irq<=0;
        if(request && !physical && address[15:1]==15'o77777)
            $fatal(1,"CPU-local PSW escaped onto external bus");
        if(ack && !error && writing)begin
            if(!byte_access)memory[{bank,address[15:1]}]<=wdata;
            else if(address[0])memory[{bank,address[15:1]}][15:8]<=wdata[7:0];
            else memory[{bank,address[15:1]}][7:0]<=wdata[7:0];
        end
        if(stopped || clocks>100000000)$fatal(1,"case%0d stopped/timeout PC%o IR%o",cases,dut.engine.dp.rf.words[7],ir);
    end
    task eq(input [15:0] got,want,input string msg);
        checks++;
        if(got!==want)$fatal(1,"case%0d %s got%o expected%o PC%o IR%o",cases,msg,got,want,dut.engine.dp.rf.words[7],ir);
    endtask
    integer fd,rc,count,bank_test,endpoint,expect_irq,ptr;
    reg [15:0] old_psw,r0,op0,op1,op2,want_r0,want_psw,trap_vector;
    string path;
    task emit(input [15:0] value);
        memory[32768+(ptr>>1)]=value;ptr+=2;
    endtask
    initial begin
        for(integer i=0;i<65536;i++)memory[i]=0;
        if(!$value$plusargs("VECTORS=%s",path))$fatal(1,"missing vectors");
        fd=$fopen(path,"r");if(!fd)$fatal(1,"cannot open vectors");
        while(!$feof(fd))begin
            rc=$fscanf(fd,"%d %h %h %d %h %h %h %h %h %h %d\n",
                       bank_test,old_psw,r0,count,op0,op1,op2,want_r0,want_psw,trap_vector,expect_irq);
            if(rc!=11)$fatal(1,"bad vector %0d fields%0d",cases,rc);
            @(negedge clk);reset=1;irq=0;repeat(4)@(negedge clk);
            cases++;delay_cycles=cases%4;
            memory[32768]=16'o2000;memory[32769]=16'o340;
            memory[32768+16'o100/2]=16'o1000;memory[32768+16'o102/2]=old_psw & ~16'o20;
            ptr=16'o2000;
            emit(16'o12706);emit(16'o3700); // MOV #stack,SP
            emit(16'o12705);emit(op0==16'o31 ? 16'd0 : 16'o177776); // service access cursor
            emit(16'o12700);emit(r0);       // MOV #initial,R0
            if(bank_test!=0)begin
                // RTI in HALT space permits T-set tests without USER trace.
                emit(16'o2);
                memory[32768+16'o3700/2]=16'o1000;memory[32768+16'o3702/2]=old_psw;
            end else emit(16'o10); // START restores guest PC/PSW
            endpoint=16'o1000;
            if(bank_test==0 && old_psw[4])begin
                memory[16'o1000/2]=16'o6; // RTT enters T-set instruction
                memory[16'o3700/2]=16'o1002;memory[16'o3702/2]=old_psw;
                endpoint+=2;
            end
            memory[(bank_test!=0?32768:0)+(endpoint>>1)]=op0;
            memory[(bank_test!=0?32768:0)+(endpoint>>1)+1]=op1;
            memory[(bank_test!=0?32768:0)+(endpoint>>1)+2]=op2;
            endpoint+=2*count;
            memory[(bank_test!=0?32768:0)+(endpoint>>1)]=16'o5201; // INC R1: prove flag interlock clears
            memory[(bank_test!=0?32768:0)+(endpoint>>1)+1]=16'o777;
            for(integer v=4;v<=16'o200;v+=2)begin
                // Fill only vectors under 040; IRQ vector 0200 is separate.
                if(v<16'o40 || v==16'o200)begin
                    memory[v>>1]=16'o4000;
                    memory[(v+2)>>1]=16'o340;
                    memory[32768+(v>>1)]=16'o4000;
                    memory[32768+((v+2)>>1)]=16'o340;
                    v+=2;
                end
            end
            memory[16'o4000/2]=16'o777;memory[32768+16'o4000/2]=16'o777;
            memory[32767]=16'h1357;memory[65535]=16'h2468;
            @(negedge clk);reset=0;
            do @(negedge clk); while(!(dut.engine.fetching && dut.engine.mem_request && address==16'(endpoint-2*count) && bank==(bank_test!=0)));
            eq(psw,old_psw & 16'hf9ff,"entry PSW");
            if(expect_irq!=0)irq=1;
            if(trap_vector==0)begin
                do @(negedge clk); while(!(dut.engine.step && dut.engine.alu_boundary && ir==op0));
                @(posedge clk);@(negedge clk);
                eq(psw,want_psw,"instruction PSW (destination takes precedence)");
                eq(dut.engine.dp.rf.words[0],want_r0,"instruction R0");
                eq(dut.engine.dp.rf.words[7],16'(endpoint),"instruction PC");
                if(old_psw[4] && bank_test==0 || expect_irq!=0 && want_psw[7:5]<7)begin
                    do @(negedge clk); while(!(request && dut.engine.fetching && address==16'o4000));
                    eq(memory[(dut.engine.dp.rf.words[6]>>1)],16'(endpoint),"trap frame PC");
                    eq(memory[(dut.engine.dp.rf.words[6]>>1)+1],want_psw,"trap frame PSW");
                    if(old_psw[4] && expect_irq!=0)eq({15'b0,irq},1,"trace before IRQ");
                end else begin
                    do @(negedge clk); while(!(dut.engine.step && dut.engine.alu_boundary && ir==16'o5201));
                    @(posedge clk);@(negedge clk);eq(psw,(want_psw & 16'hfff1),"next INC updates NZV");
                end
            end else begin
                do @(negedge clk); while(!(request && dut.engine.fetching && address==16'o4000));
                eq(psw,16'o340,"fault vector PSW");
                eq(dut.engine.dp.rf.words[0],r0,"failed reference preserves R0");
                if(bank_test==0)begin
                    eq(memory[(dut.engine.dp.rf.words[6]>>1)],16'(endpoint),"fault saved PC");
                    eq(memory[(dut.engine.dp.rf.words[6]>>1)+1],old_psw & 16'hf9ff,"fault saved PSW");
                end
            end
            eq(memory[32767],op0==16'o45 ? r0 : 16'h1357,"USER physical last word");
            eq(memory[65535],op0==16'o41 ? r0 : 16'h2468,"HALT physical last word");
        end
        $display("PASS CP78 PSW: %0d cases %0d checks %0d clocks decode=%0d aligned=%0d",cases,checks,clocks,ROM_DECODE,ALIGNED_WORD_READS);
        $finish;
    end
endmodule
