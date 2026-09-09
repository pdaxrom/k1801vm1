// CP16: failed-operation checks end at BUS_FAULT_ENTRY before vector traffic.
`timescale 1ns/1ps
module tb_control_faults;
    reg clk=0,reset=1,active=0;
    reg [15:0] opcode,stack_word;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    reg [1:0] observed_fault=0;
    always @(posedge clk)begin
        if(reset)observed_fault<=0;
        else if(dut.engine.fault_redirect)begin
            observed_fault<=dut.engine.bus_fault;
            if(dut.engine.step || rf_write)$fatal(1,"failed memory operation committed");
        end
    end
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer c,k,waits,age=0,cycles,beats=0,reads=0,writes=0;
    reg pending=0;reg [34:0] held;
    reg [15:0] counter_value;
    wire ack=request && age==waits;
    wire error=addr!=0 && ((c>=16 && c<24 && writing) ||
                           (c>=24 && c<34 && reading));
    wire [15:0] trap_vector=c<8 ? 16'd4 : 16'd8;
    wire [15:0] rdata=addr==0 ? opcode : c<16 ? (addr==trap_vector ? 16'h4000 : 16'he9) :
                       addr==2 ? 16'h2000 : stack_word;
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
        .mem_read(reading),.mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),
        .debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk)begin
        if(reset)begin beats=0;reads=0;writes=0;age<=0;pending<=0;end
        else if(active && request)begin
            if(byte_access || reading==writing || addr[0])$fatal(1,"control invalid bus");
            if(pending && held!=={reading,writing,byte_access,addr,wdata})$fatal(1,"control request changed");
            if(ack)begin
                beats=beats+1;age<=0;pending<=0;
                if(addr!=0)begin
                    if(reading)reads=reads+1;else writes=writes+1;
                    if(c<16)begin
                        case(beats)
                            2:if(!reading || addr!==trap_vector+16'd2)$fatal(1,"invalid-mode PSW vector");
                            3:if(!reading || addr!==trap_vector)$fatal(1,"invalid-mode PC vector");
                            4:if(!writing || addr!==16'h5ffe || wdata!==3)$fatal(1,"invalid-mode old PSW frame");
                            5:if(!writing || addr!==16'h5ffc || wdata!==2)$fatal(1,"invalid-mode old PC frame");
                            default:$fatal(1,"extra invalid-mode bus beat");
                        endcase
                    end else if(addr!=2 && addr!=16'h6000 && addr!=16'h5ffe)$fatal(1,"unexpected control access %h",addr);
                    if(writing && !error)stack_word<=wdata;
                    if(c>=16 && c<24 && wdata!==(c==22?16'h5ffe:c==23?16'h0002:16'(16'h2000+(c-16)*16'h0100)))
                        $fatal(1,"JSR stack value/order c%0d %h",c,wdata);
                end
            end else begin age<=age+1;pending<=1;held<={reading,writing,byte_access,addr,wdata};end
        end else if(active && pending)$fatal(1,"control request dropped");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(c=0;c<42;c=c+1)begin
            @(negedge clk);reset=1;active=0;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=c%4;stack_word=16'h3456;
            for(k=0;k<7;k=k+1)dut.engine.dp.rf.words[k]=16'(16'h2000+k*16'h0100);
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.status.psw=3;
            if(c<8)opcode=16'o000100+16'(c);
            else if(c<16)opcode=16'o004000+16'((c-8)<<6);
            else if(c<24)opcode=16'o004010+16'((c-16)<<6);
            else if(c<32)opcode=16'o000200+16'(c-24);
            else case(c)
                32:opcode=16'o000137;
                33:opcode=16'o004737;
                34:begin opcode=16'o004510;dut.engine.dp.rf.words[6]=16'h6001;end
                35:begin opcode=16'o000205;dut.engine.dp.rf.words[6]=16'h6001;end
                36:begin opcode=16'o000130;dut.engine.dp.rf.words[0]=16'h2001;end
                37:begin opcode=16'o004530;dut.engine.dp.rf.words[0]=16'h2001;end
                38:begin opcode=16'o000110;dut.engine.dp.rf.words[0]=16'h2001;end
                39:begin opcode=16'o004510;dut.engine.dp.rf.words[0]=16'h2001;end
                40:begin opcode=16'o000110;dut.engine.dp.rf.words[0]=16'o177562;end
                41:begin opcode=16'o004510;dut.engine.dp.rf.words[0]=16'o177562;end
            endcase
            active=1;cycles=0;
            while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(c<16)begin
                if(!retire || stopped || psw!==16'he9 || beats!=5 || reads!=2 || writes!=2 ||
                   stack_word!==2 || dut.engine.dp.rf.words[6]!==16'h5ffc || dut.engine.dp.rf.words[7]!==16'h4000)
                    $fatal(1,"invalid-mode trap outcome c%0d",c);
            end
            if(c>=16 && psw!==3)$fatal(1,"control fault changed PSW c%0d",c);
            if(c>=16 && c<38)begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(c<16?2'd3:c<34?2'd2:2'd1))$fatal(1,"control fault c%0d",c);
                if(stack_word!==16'h3456)$fatal(1,"failed operation wrote stack");
                if(beats!==(c<16 || c>=34?1:2))$fatal(1,"control fault beat count c%0d %0d",c,beats);
                if(c>=16 && c<24 && (dut.engine.dp.rf.words[7]!==2 || dut.engine.dp.rf.words[6]!==16'h5ffe))
                    $fatal(1,"JSR failed push committed link/target");
                if(c>=24 && c<32 && dut.engine.dp.rf.words[6]!==16'h6000)$fatal(1,"RTS failed read popped SP");
                if(c==33 && dut.engine.dp.rf.words[6]!==16'h6000)$fatal(1,"JSR pointer fault pushed stack");
                if(observed_fault!=0 && dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            end else if(c>=38)begin
                if(!retire || stopped || fault || reads || writes!==(c%2) || beats!==(1+c%2))
                    $fatal(1,"JMP/JSR read final target c%0d",c);
                if(dut.engine.dp.rf.words[7]!==(c<40?16'h2001:16'o177562))$fatal(1,"control target c%0d",c);
                if(c%2 && (stack_word!==16'h2500 || dut.engine.dp.rf.words[5]!==2))$fatal(1,"JSR link/stack c%0d",c);
                if(c<40)begin
                    k=beats;tick;
                    if(stopped || upc!==10'h015 || observed_fault!==1 || beats!=k)$fatal(1,"odd target not rejected at next fetch");
                end
            end
            active=0;
        end
        // A==1 must compare all 16 bits, independently of old PSW.Z. Each
        // high bit with low bit 1 must take SOB, while exactly 1 terminates.
        for(k=0;k<16;k=k+1)begin
            @(negedge clk);reset=1;active=0;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=k%4;counter_value=(16'b1<<k)|16'b1;
            dut.engine.dp.rf.words[0]=counter_value;dut.engine.status.psw=15;
            opcode=16'o077001;active=1;cycles=0;
            while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<100)begin tick;cycles=cycles+1;end
            if(!retire || stopped || psw!==15 || beats!=1 ||
               dut.engine.dp.rf.words[0]!==counter_value-16'd1 ||
               dut.engine.dp.rf.words[7]!==(counter_value==1?16'd2:16'd0))
                $fatal(1,"SOB early predicate bit%0d",k);
            active=0;
        end
        $display("PASS control directed: 58 invalid-mode, pointer/stack errors, aliases, odd/CSR targets and 16-bit SOB predicate cases");$finish;
    end
    initial begin #1000000;$fatal(1,"control directed timeout");end
endmodule
