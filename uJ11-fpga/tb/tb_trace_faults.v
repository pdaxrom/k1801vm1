`timescale 1ns/1ps
// The defined v1 second-fault policy is diagnostic STOP, not red-stack recovery.
module tb_trace_faults;
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer operation,kind,waits,age=0,beats=0,frames=0,cycles,cases=0,k,redirects=0,traces=0,retired=0;
    reg held=0;reg [34:0] held_bus;
    reg [15:0] opcode,pc,sp,pushed_psw,pushed_pc,frame_sp,old_psw,old_pc;
    wire ack=request && age>=waits;
    wire in_frame=dut.engine.frame_active;
    wire error=in_frame ? kind>0 && kind<5 && frames==kind-1 :
               operation==4 ? addr==pc : operation==3 ? addr==16'h2000 : 1'b0;
    wire [15:0] rdata=addr==16'h1000 ? opcode : addr==(operation<3?16'd14:16'd6)?16'h00a9 :
        addr==(operation<3?16'd12:16'd4)?16'h3000 : addr==sp?16'h1800 : addr==sp+16'd2?16'h53:16'hdead;
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),
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
                        0:if(!reading || addr!==(operation<3?16'd14:16'd6))$fatal(1,"fault vector PSW order");
                        1:if(!reading || addr!==(operation<3?16'd12:16'd4))$fatal(1,"fault vector PC order");
                        2:begin
                            if(!writing || addr!==frame_sp-16'd2 || wdata!==old_psw)$fatal(1,"fault old PSW push");
                            if(!error)pushed_psw<=wdata;
                        end
                        3:begin
                            if(!writing || addr!==frame_sp-16'd4 || wdata!==old_pc)$fatal(1,"fault old PC push");
                            if(!error)pushed_pc<=wdata;
                        end
                        default:$fatal(1,"recursive fault frame");
                    endcase
                end
            end
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(operation=0;operation<6;operation=operation+1)
        for(kind=0;kind<6;kind=kind+1)for(waits=0;waits<4;waits=waits+1)if(!(operation==2 && kind==5))begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);pc=operation==5?16'h1001:16'h1000;sp=kind==5?16'h6001:16'h6000;
            frame_sp=operation==2?sp+16'd4:sp;old_pc=operation>=4?pc:operation==2?16'h1800:pc+16'd2;
            old_psw=operation==0?16'h51:operation==1?16'h59:16'h53;
            opcode=operation==0?16'o010001:operation==1?16'o020001:operation==2?16'o000002:16'o011001;
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h2000+16'(k*16'h100);
            dut.engine.dp.rf.words[6]=sp;dut.engine.dp.rf.words[7]=pc;dut.engine.status.psw=16'h53;
            cycles=0;while(!((redirects+traces)==1 && upc==10'h020 && !dut.engine.irq_active) && !stopped && cycles<200)begin tick;cycles=cycles+1;end
            if(redirects!=(operation>=3?1:0) || traces!=(operation<3?1:0) || retired+(retire?1:0)!=(operation<3?1:0))$fatal(1,"trace/fault entry/retire count");
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==(operation==0 && k==1?16'h2000:16'h2000+16'(k*16'h100)))$fatal(1,"fault clobbered registers");
            if(kind==0)begin
                if(stopped || fault || frames!=4 || psw!==16'ha9 || dut.engine.dp.rf.words[7]!==16'h3000 ||
                   dut.engine.dp.rf.words[6]!==frame_sp-16'd4 || pushed_psw!==old_psw || pushed_pc!==old_pc)$fatal(1,"fault frame success");
            end else begin
                if(!stopped || fault!==(kind==5?2'd1:2'd2) || frames!=(kind==5?2:kind) ||
                   dut.engine.dp.rf.words[7]!==old_pc ||
                   psw!==(kind<=2?old_psw:16'ha9) ||
                   dut.engine.dp.rf.words[6]!==(kind<=2?frame_sp:kind==4?frame_sp-16'd4:frame_sp-16'd2) ||
                   pushed_psw!==(kind==4?old_psw:16'hdead) || pushed_pc!==16'hdead)$fatal(1,"double fault terminal state op%0d kind%0d",operation,kind);
                k=beats;repeat(8)tick;if(beats!=k || request || !stopped)$fatal(1,"double fault not terminal");
            end
            cases=cases+1;
        end
        $display("PASS trace faults: %0d trace/RTI frames and T-set fault priority cases, all four frame errors, odd SP and 0..3 waits",cases);$finish;
    end
    initial begin #3000000;$fatal(1,"double fault timeout");end
endmodule
