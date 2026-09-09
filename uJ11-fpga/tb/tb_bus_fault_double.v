`timescale 1ns/1ps
// The defined v1 second-fault policy is diagnostic STOP, not red-stack recovery.
module tb_bus_fault_double;
    reg clk=0,reset=1;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer operation,kind,waits,age=0,beats=0,frames=0,cycles,cases=0,k,redirects=0;
    reg held=0;reg [34:0] held_bus;
    reg [15:0] opcode,pc,sp,pushed_psw,pushed_pc;
    wire ack=request && age>=waits;
    wire in_frame=dut.engine.frame_active;
    wire error=in_frame ? kind>0 && kind<5 && frames==kind-1 :
               operation==0 ? addr==pc : operation>=2 ? addr==16'h2000 : 1'b0;
    wire [15:0] rdata=addr==16'h1000 ?opcode:addr==6?16'h00a9:addr==4?16'h3000:16'hdead;
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
        if(reset)begin age<=0;held<=0;beats<=0;frames<=0;redirects<=0;pushed_pc<=16'hdead;pushed_psw<=16'hdead;end
        else begin
            if(retire)$fatal(1,"failed instruction/frame retired");
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
                        0:if(!reading || addr!==6)$fatal(1,"fault vector PSW order");
                        1:if(!reading || addr!==4)$fatal(1,"fault vector PC order");
                        2:begin
                            if(!writing || addr!==sp-16'd2 || wdata!==16'h43)$fatal(1,"fault old PSW push");
                            if(!error)pushed_psw<=wdata;
                        end
                        3:begin
                            if(!writing || addr!==sp-16'd4 || wdata!==(operation<2?pc:pc+16'd2))$fatal(1,"fault old PC push");
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
        for(operation=0;operation<4;operation=operation+1)
        for(kind=0;kind<6;kind=kind+1)for(waits=0;waits<4;waits=waits+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);pc=operation==1?16'h1001:16'h1000;sp=kind==5?16'h6001:16'h6000;
            opcode=operation==3?16'o010110:16'o011001;
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h2000+16'(k*16'h100);
            dut.engine.dp.rf.words[6]=sp;dut.engine.dp.rf.words[7]=pc;dut.engine.status.psw=16'h43;
            cycles=0;while(!(redirects==1 && upc==10'h020 && !dut.engine.irq_active) && !stopped && cycles<200)begin tick;cycles=cycles+1;end
            if(redirects!=1 || retire)$fatal(1,"fault entry count");
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==16'h2000+16'(k*16'h100))$fatal(1,"fault clobbered registers");
            if(kind==0)begin
                if(stopped || fault || frames!=4 || psw!==16'ha9 || dut.engine.dp.rf.words[7]!==16'h3000 ||
                   dut.engine.dp.rf.words[6]!==sp-16'd4 || pushed_psw!==16'h43 || pushed_pc!==(operation<2?pc:pc+16'd2))$fatal(1,"fault frame success");
            end else begin
                if(!stopped || fault!==(kind==5?2'd1:2'd2) || frames!=(kind==5?2:kind) ||
                   dut.engine.dp.rf.words[7]!==(operation<2?pc:pc+16'd2) ||
                   psw!==(kind<=2?16'h43:16'ha9) ||
                   dut.engine.dp.rf.words[6]!==(kind<=2?sp:kind==4?sp-16'd4:sp-16'd2) ||
                   pushed_psw!==(kind==4?16'h43:16'hdead) || pushed_pc!==16'hdead)$fatal(1,"double fault terminal state op%0d kind%0d",operation,kind);
                k=beats;repeat(8)tick;if(beats!=k || request || !stopped)$fatal(1,"double fault not terminal");
            end
            cases=cases+1;
        end
        $display("PASS bus double fault: %0d FETCH/operand-error and odd-PC cases with all four frame errors, odd SP and 0..3 waits",cases);$finish;
    end
    initial begin #3000000;$fatal(1,"double fault timeout");end
endmodule
