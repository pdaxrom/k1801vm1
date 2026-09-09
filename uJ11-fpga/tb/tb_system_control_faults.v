`timescale 1ns/1ps
// The defined v1 second-fault policy is diagnostic STOP, not red-stack recovery.
module tb_system_control_faults;
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire irq_ack;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer operation,irq_present,kind,waits,age=0,beats=0,frames=0,cycles,cases=0,k,redirects=0,traces=0,retired=0;
    reg held=0;reg [34:0] held_bus;
    reg [15:0] opcode,pc,sp,pushed_psw,pushed_pc,frame_sp,old_psw,old_pc;
    wire ack=request && age>=waits;
    wire in_frame=dut.engine.frame_active;
    wire error=in_frame && kind<4 && frames==kind-1;
    wire [15:0] rdata=addr==16'h1000 ? 16'b0 : addr==16'd4 ? 16'h3001 : 16'hdead;
    uj11_core dut(.irq_valid(irq_present!=0),.irq_priority(3'd7),.irq_vector(8'h40),.irq_ack(irq_ack),.waiting(),.peripheral_reset(),
        .clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),.mem_request(request),
        .mem_read(reading),.mem_write(writing),.mem_byte(byte_access),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),
        .debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk)begin
        if(reset)begin age<=0;held<=0;beats<=0;frames<=0;redirects<=0;traces<=0;retired<=0;pushed_pc<=16'hdead;pushed_psw<=16'hdead;end
        else begin
            if(irq_ack)$fatal(1,"IRQ acknowledged inside failed HALT");
            if(retire)retired<=retired+1;
            if(dut.engine.trace_ack)traces<=traces+1;
            if(dut.engine.fault_redirect)redirects<=redirects+1;
            if(held && (!request || held_bus!=={reading,writing,byte_access,addr,wdata}))$fatal(1,"double fault bus stability");
            held<=request&&!ack;held_bus<={reading,writing,byte_access,addr,wdata};
            if(!request || ack)age<=0;else age<=age+1;
            if(request && ack)begin
                if(addr[0] || byte_access || reading==writing)$fatal(1,"bad word bus");
                beats<=beats+1;
                if(in_frame)begin
                    frames<=frames+1;
                    case(frames)
                        0:begin
                            if(!writing || addr!==sp-16'd2 || wdata!==old_psw)$fatal(1,"HALT old PSW push");
                            if(!error)pushed_psw<=wdata;
                        end
                        1:begin
                            if(!writing || addr!==sp-16'd4 || wdata!==pc+16'd2)$fatal(1,"HALT old PC push");
                            if(!error)pushed_pc<=wdata;
                        end
                        2:if(!reading || addr!==16'd4)$fatal(1,"HALT restart PC read");
                        default:$fatal(1,"recursive HALT frame");
                    endcase
                end
            end
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(operation=0;operation<2;operation=operation+1)
        for(irq_present=0;irq_present<2;irq_present=irq_present+1)
        for(kind=1;kind<5;kind=kind+1)for(waits=0;waits<4;waits=waits+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);pc=16'h1000;sp=kind==4?16'h6001:16'h6000;old_psw=operation?16'h13:16'h03;
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h2000+16'(k*16'h100);
            dut.engine.dp.rf.words[6]=sp;dut.engine.dp.rf.words[7]=pc;dut.engine.status.psw=old_psw;
            cycles=0;while(!stopped && cycles<200)begin tick;cycles=cycles+1;end
            if(redirects || traces || retired || retire || psw!==old_psw || dut.engine.dp.rf.words[7]!==pc+16'd2)
                $fatal(1,"HALT fault committed/redirected state");
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==16'h2000+16'(k*16'h100))$fatal(1,"HALT clobbered registers");
            if(!stopped || fault!==(kind==4?2'd1:2'd2) || frames!=(kind==4?0:kind) ||
               dut.engine.dp.rf.words[6]!==sp-((kind==1 || kind==4)?16'd2:16'd4) ||
               pushed_psw!==((kind==2 || kind==3)?old_psw:16'hdead) ||
               pushed_pc!==(kind==3?pc+16'd2:16'hdead))$fatal(1,"HALT fault terminal state kind%0d",kind);
            k=beats;repeat(8)tick;if(beats!=k || request || !stopped)$fatal(1,"HALT fault not terminal");
            cases=cases+1;
        end
        $display("PASS system control faults: %0d HALT frames; all three ACK errors, odd SP, T/IRQ combinations and 0..3 waits",cases);$finish;
    end
    initial begin #3000000;$fatal(1,"HALT fault timeout");end
endmodule
