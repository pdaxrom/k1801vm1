`timescale 1ns/1ps
module tb_byte_io;
    reg clk=0,reset=1;
    reg [15:0] opcode,csr;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    reg [1:0] observed_fault=0;
    always @(posedge clk)begin
        if(reset)observed_fault<=0;
        else if(dut.engine.fault_redirect)begin
            observed_fault<=dut.engine.bus_fault;
            if(dut.engine.step || rf_write)$fatal(1,"failed byte operation committed");
        end
    end
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer op,kind,waits,age=0,cycles,reads=0,writes=0,attempts=0,checks=0;
    reg pending=0;reg [34:0] held_request;
    wire ack=request && age==waits;
    wire error=addr!=0 && ((kind==2 && reading) || (kind==3 && writing));
    wire [15:0] rdata=addr==0?opcode:csr;
    reg [7:0] values[0:4];reg [3:0] flags[0:4];
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
        if(reset)begin reads=0;writes=0;attempts=0;age<=0;pending<=0;end
        else if(request)begin
            if(pending && {reading,writing,byte_access,addr,wdata}!==held_request)$fatal(1,"byte I/O request changed");
            if(reading==writing || (addr==0?byte_access:!byte_access))$fatal(1,"byte I/O direction/width");
            if(ack)begin
                age<=0;pending<=0;attempts=attempts+1;
                if(addr!=0)begin
                    if({addr[15:1],1'b0}!==16'o177562)$fatal(1,"byte I/O address %o",addr);
                    if(reading)reads=reads+1;else writes=writes+1;
                    if(writing && !error)csr<={csr[15:8],wdata[7:0]};
                end
            end else begin
                pending<=1;held_request<={reading,writing,byte_access,addr,wdata};age<=age+1;
            end
        end else if(pending)$fatal(1,"byte I/O request dropped");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        values[0]=8'ha5;values[1]=8'h3c;values[2]=8'h3c;values[3]=8'h18;values[4]=8'hbd;
        flags[0]=9;flags[1]=2;flags[2]=1;flags[3]=1;flags[4]=9;
        #100;
        for(op=0;op<5;op=op+1)for(kind=0;kind<5;kind=kind+1)
        if(!(kind==3 && (op==1 || op==2)))begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=(op+kind)%4;csr=16'h5a3c;
            opcode=16'o110011+(op<<12);dut.engine.dp.rf.words[0]=16'ha5a5;
            dut.engine.dp.rf.words[1]=16'o177562+(kind==1);dut.engine.status.psw=3;
            if(kind==2)begin opcode=16'o111001+(op<<12);dut.engine.dp.rf.words[0]=16'o177563;dut.engine.dp.rf.words[1]=16'ha55a;end
            if(kind==4)begin opcode=16'o110031+(op<<12);dut.engine.dp.rf.words[1]=16'o177563;end
            cycles=0;while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(kind<2)begin
                if(stopped || !retire || csr!=={8'h5a,values[op]} || psw!=={12'b0,flags[op]})
                    $fatal(1,"byte I/O result op%0d kind%0d csr%h psw%h",op,kind,csr,psw);
                if(reads!=(op==0?0:1) || writes!=((op==1 || op==2)?0:1))$fatal(1,"byte I/O side effect count");
            end else begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(kind==4?2'd1:2'd2) || psw!==3 || csr!==16'h5a3c)
                    $fatal(1,"byte I/O fault commit op%0d kind%0d",op,kind);
                if(kind==2 && (reads!=1 || writes!=0 || dut.engine.dp.rf.words[1]!==16'ha55a))$fatal(1,"byte read error commit");
                if(kind==3 && (writes!=1 || reads!=(op==0?0:1)))$fatal(1,"byte write error count");
                if(kind==4 && attempts!=1)$fatal(1,"byte instruction let odd WORD pointer reach bus");
                if(dut.engine.seq.link_valid)$fatal(1,"byte fault left CALL link live");
            end
            checks=checks+1;
        end
        if(checks!=23)$fatal(1,"byte I/O checks");
        $display("PASS byte I/O: 23 even/odd CSR, read/write error and odd-word pointer cases");$finish;
    end
    initial begin #1000000;$fatal(1,"byte I/O timeout");end
endmodule
