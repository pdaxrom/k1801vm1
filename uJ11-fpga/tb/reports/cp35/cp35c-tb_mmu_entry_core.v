`timescale 1ns/1ps
// Sequential equivalence against the unchanged CPU. Reference clock edges
// are omitted only during the private excursion/hold; never gate a clock
// combinationally with a state bit that changes on that same rising edge.
module tb_mmu_entry_core #(parameter integer CASES=69632);
    reg clk=0,refclk=0,reset=1,active=0,mmu_enabled=1,mmu_hold=0;
    reg irq_valid=0;
    reg [2:0] irq_priority=0;
    reg [7:0] waits=0;
    wire [15:0] ca,cw,ci,cm,cp,cq,crd,ra,rw,ri,rm,rp,rq,rrd,rdata,ram_data;
    wire creq,cr,cwr,cb,cs,cret,crwe,cirq,cwait,cinit;
    wire rreq,rr,rwr,rb,rs,rret,rrwe,rirq,rwait,rinit,ack,error,ram_ack;
    wire [1:0] cf,rf;
    wire [3:0] craddr,rraddr;
    wire [9:0] cu,ru;
    wire [35:0] cword,rword;
    wire [31:0] transactions,writes;
    integer test_id,i,k,cycles,normal_edges=0,extra_edges=0,checks=0,beats=0;
    integer fault_at=-1,entries=0,returns=0,held_edges=0,disabled_cases=0;
    integer seed_cases=0,irq_cases=0;
    reg stepped,was_active,was_entry,was_return,was_hold;
    reg [15:0] saved_psw,opcode;
    reg [10:0] saved_link;
    reg [31:0] random_state=32'hb29df18a;
    reg [1023:0] covered=0;
    wire inject_error=active && creq && fault_at>=0 && beats==fault_at;
    assign ack=inject_error || ram_ack;
    assign error=inject_error;
    assign rdata=inject_error ? 16'b0 : ram_data;
    uj11_core #(.ROM_DECODE(1)) dut(.clk(clk),.reset(reset),
        .mmu_enabled(mmu_enabled),.mmu_hold(mmu_hold),.irq_valid(irq_valid),
        .irq_priority(irq_priority),.irq_vector(8'o040),.irq_ack(cirq),.waiting(cwait),
        .peripheral_reset(cinit),.mem_addr(ca),.mem_write_data(cw),.mem_request(creq),
        .mem_read(cr),.mem_write(cwr),.mem_byte(cb),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(cs),.fault_code(cf),.retire(cret),.debug_upc(cu),
        .debug_uword(cword),.ir(ci),.mdr(cm),.psw(cp),.q(cq),.debug_rf_write(crwe),
        .debug_rf_address(craddr),.debug_rf_data(crd));
    uj11_core_reference #(.ROM_DECODE(1)) reference_cpu(.clk(refclk),.reset(reset),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(8'o040),
        .irq_ack(rirq),.waiting(rwait),.peripheral_reset(rinit),.mem_addr(ra),
        .mem_write_data(rw),.mem_request(rreq),.mem_read(rr),.mem_write(rwr),
        .mem_byte(rb),.mem_ack(ack),.mem_error(error),.mem_read_data(rdata),
        .stopped(rs),.fault_code(rf),.retire(rret),.debug_upc(ru),.debug_uword(rword),
        .ir(ri),.mdr(rm),.psw(rp),.q(rq),.debug_rf_write(rrwe),
        .debug_rf_address(rraddr),.debug_rf_data(rrd));
    uj11_ram memory(.clk(clk),.reset(reset),.request(creq && !inject_error),
        .reading(cr),.writing(cwr),.byte_access(cb),.addr(ca),.write_data(cw),
        .wait_states(waits),.ack(ram_ack),.read_data(ram_data),
        .transactions(transactions),.writes(writes));
    always #5 clk=~clk;
    always @(negedge clk)refclk=0;
    always @(posedge clk)begin
        stepped=reset || (!dut.engine.mmu_block_memory && !dut.engine.mmu_stall);
        was_active=dut.engine.mmu_active;
        was_entry=dut.engine.mmu_redirect && !was_active;
        was_return=dut.engine.mmu_redirect && was_active;
        was_hold=dut.engine.mmu_stall;
        if(reset || stepped)refclk=1;
        if(active && !reset)begin
            if(was_entry)begin
                saved_psw=cp;saved_link={dut.engine.seq.link_valid,dut.engine.seq.link};
                covered[cu]=1;entries=entries+1;
            end
            if(was_return)returns=returns+1;
            if(was_hold)held_edges=held_edges+1;
            if(dut.engine.mmu_block_memory && (creq || cr || cwr || cirq || cwait || cinit))
                $fatal(1,"private routine leaked external event case%0d upc%h",test_id,cu);
            if(was_active && crwe && craddr<13)
                $fatal(1,"private RF write outside T5..T7");
            if(stepped)begin
                if({creq,cr,cwr,cb,ca}!=={rreq,rr,rwr,rb,ra} || (cwr && cw!==rw))
                    $fatal(1,"guest bus mismatch case%0d upc%h/%h",test_id,cu,ru);
                normal_edges=normal_edges+1;
            end else extra_edges=extra_edges+1;
            if(creq && ack)beats<=beats+1;
        end
        #1;
        if(active && !reset)begin
            if({ci,cm,cq,cs,cf,cwait,cinit}!=={ri,rm,rq,rs,rf,rwait,rinit})
                $fatal(1,"context mismatch case%0d upc%h/%h",test_id,cu,ru);
            for(k=0;k<13;k=k+1)if(dut.engine.dp.rf.words[k]!==reference_cpu.engine.dp.rf.words[k])
                $fatal(1,"live RF mismatch case%0d R/T index%0d",test_id,k);
            if({dut.engine.seq.link_valid,dut.engine.seq.link}!==
               {reference_cpu.engine.seq.link_valid,reference_cpu.engine.seq.link})
                $fatal(1,"CALL link mismatch case%0d upc%h",test_id,cu);
            if(was_active && {dut.engine.seq.link_valid,dut.engine.seq.link}!==saved_link)
                $fatal(1,"CALL link changed in routine");
            if((!dut.engine.mmu_active || was_return) && cp!==rp)
                $fatal(1,"PSW restore mismatch case%0d got%h expected%h",test_id,cp,rp);
            if(!dut.engine.mmu_block_memory && {cu,cword}!=={ru,rword})
                $fatal(1,"memory continuation mismatch case%0d %h/%h",test_id,cu,ru);
            if(stepped && cret!==rret)$fatal(1,"retirement mismatch");
            if(was_active && !was_hold && cu==10'h1cf &&
               cp[3:0]!=={!saved_psw[15],saved_psw==16'hffff,1'b0,1'b1})
                $fatal(1,"helper word flags depend on guest byte opcode case%0d",test_id);
        end
    end
    task tick;begin @(posedge clk);#2;end endtask
    task poke(input [15:0] address,value);
        begin memory.bytes[address]=value[7:0];memory.bytes[address+16'd1]=value[15:8];end
    endtask
    initial begin
        for(i=0;i<65536;i=i+2)poke(i[15:0],16'h0000);
        #100;
        for(test_id=0;test_id<CASES;test_id=test_id+1)begin
            @(negedge clk);active=0;reset=1;mmu_hold=0;irq_valid=0;tick;
            @(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            opcode=16'(test_id<65536 ? test_id : random_state);
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            for(i=0;i<6;i=i+1)begin
                dut.engine.dp.rf.words[i]=16'h1000+16'(i*8);
                reference_cpu.engine.dp.rf.words[i]=16'h1000+16'(i*8);
                poke(16'h1000+16'(i*8),16'h1040+16'(i*8));
                poke(16'h1040+16'(i*8),random_state[15:0]);
            end
            dut.engine.dp.rf.words[6]=16'h3000;reference_cpu.engine.dp.rf.words[6]=16'h3000;
            dut.engine.dp.rf.words[7]=16'h0200;reference_cpu.engine.dp.rf.words[7]=16'h0200;
            dut.engine.status.psw=random_state[15:0];reference_cpu.engine.status.psw=random_state[15:0];
            poke(16'h0200,opcode);poke(16'h0202,16'h0004);poke(16'h0204,16'h0000);
            poke(16'h0004,16'h0400);poke(16'h0006,16'h00e0);
            poke(16'h000c,16'h0400);poke(16'h000e,16'h00e0);
            poke(16'h0008,16'h0400);poke(16'h000a,16'h00e0);
            poke(16'h0018,16'h0400);poke(16'h001a,16'h00e0);
            poke(16'h001c,16'h0400);poke(16'h001e,16'h00e0);
            poke(16'h0020,16'h0400);poke(16'h0022,16'h00e0);poke(16'h0400,16'h0000);
            waits={6'b0,test_id[1:0]};mmu_enabled=(test_id%17)!=0;
            if(!mmu_enabled)disabled_cases=disabled_cases+1;
            fault_at=test_id[5] ? {29'b0,test_id[4:2]} : -1;
            irq_valid=test_id[6];irq_priority=3'd6;
            if(irq_valid)irq_cases=irq_cases+1;
            active=1;beats=0;cycles=0;mmu_hold=0;
            while(!cret && !cs && !cwait && cycles<12000)begin
                tick;cycles=cycles+1;
                @(negedge clk);mmu_hold=dut.engine.mmu_active && (cycles%4==0 || cycles%11==0);
            end
            if(cycles==12000)$fatal(1,"instruction timeout case%0d opcode%o",test_id,opcode);
            if(cret!==rret || cs!==rs || cwait!==rwait)$fatal(1,"terminal state mismatch");
            if(cp!==rp)$fatal(1,"terminal PSW mismatch");
            active=0;checks=checks+1;
        end
        for(i=0;i<1024;i=i+1)if(covered[i])begin
            seed_cases=seed_cases+1;$display("ENTRY memory_upc=%03x",i[9:0]);
        end
        $display("PASS entry CPU miter: %0d cases, %0d normal edges, %0d extra edges, %0d entries / %0d returns, %0d held edges, %0d memory words, %0d disabled / %0d IRQ cases",
            checks,normal_edges,extra_edges,entries,returns,held_edges,seed_cases,disabled_cases,irq_cases);
        if(entries!=returns || extra_edges!=9*entries+held_edges)$fatal(1,"entry cycle accounting");
        $finish;
    end
endmodule
