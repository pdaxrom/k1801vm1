// CP16: failed-operation checks end at BUS_FAULT_ENTRY before vector traffic.
`timescale 1ns/1ps
// Exact I/O access counts and late PSW commit on all unary memory operations.
module tb_single_faults #(parameter BYTE_VARIANT=0);
    reg clk=0,reset=1;
    reg [15:0] opcode,csr;
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
    integer operation,kind,waits,age=0,cycles,reads=0,writes=0,attempts=0,cases=0;
    reg pending=0;
    reg [33:0] held_request;
    wire ack=request && age==waits;
    wire error=addr!=0 && ((kind==1 && reading) || (kind==2 && writing));
    wire [15:0] rdata=addr==0 ? opcode : csr;
    reg [15:0] expected_value[0:11];
    reg [3:0] expected_flags[0:11];
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
            if(pending && {reading,writing,addr,wdata}!==held_request)$fatal(1,"request changed before ACK");
            if((addr[0] && !byte_access) || (addr!=0 && byte_access!=BYTE_VARIANT) || reading==writing)$fatal(1,"invalid external request");
            if(ack)begin
                age<=0;pending<=0;attempts=attempts+1;
                if(addr!=0)begin
                    if({addr[15:1],1'b0}!==16'o177562)$fatal(1,"unexpected operand address %o",addr);
                    if(reading)reads=reads+1;else writes=writes+1;
                    if(writing && !error)csr<=byte_access?{csr[15:8],wdata[7:0]}:wdata;
                end
            end else begin
                pending<=1;held_request<={reading,writing,addr,wdata};age<=age+1;
            end
        end else if(pending)$fatal(1,"request dropped before ACK");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        expected_value[0]=0;expected_flags[0]=4;
        expected_value[1]=16'h7ffe;expected_flags[1]=1;
        expected_value[2]=16'h8002;expected_flags[2]=9;
        expected_value[3]=16'h8000;expected_flags[3]=9;
        expected_value[4]=16'h7fff;expected_flags[4]=1;
        expected_value[5]=16'h8002;expected_flags[5]=8;
        expected_value[6]=16'h8000;expected_flags[6]=8;
        expected_value[7]=16'h8001;expected_flags[7]=8;
        expected_value[8]=16'hc000;expected_flags[8]=9;
        expected_value[9]=3;expected_flags[9]=3;
        expected_value[10]=16'hc000;expected_flags[10]=9;
        expected_value[11]=2;expected_flags[11]=3;
        if(BYTE_VARIANT)begin
            expected_value[0]=16'ha500;expected_value[1]=16'ha57e;
            expected_value[2]=16'ha582;expected_value[3]=16'ha580;
            expected_value[4]=16'ha57f;expected_value[5]=16'ha582;
            expected_value[6]=16'ha580;expected_value[7]=16'ha581;
            expected_value[8]=16'ha5c0;expected_value[9]=16'ha503;
            expected_value[10]=16'ha5c0;expected_value[11]=16'ha502;
        end
        #100;
        for(operation=0;operation<12;operation=operation+1)
        for(kind=0;kind<4;kind=kind+1)
        if(!(kind==1 && operation==0) && !(kind==2 && operation==7))begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=(operation+kind)%4;csr=BYTE_VARIANT?16'ha581:16'h8001;
            opcode=(BYTE_VARIANT?16'o105011:16'o005011)+(operation<<6);
            if(BYTE_VARIANT && kind==3)opcode=opcode+16'o20; // odd WORD deferred pointer
            dut.engine.dp.rf.words[1]=kind==3 ? 16'o177563 : (16'o177562 | (BYTE_VARIANT && operation%2));
            dut.engine.status.psw=3;
            cycles=0;while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(kind==0)begin
                if(stopped || !retire || fault!=0 || psw!=={12'b0,expected_flags[operation]} || csr!==expected_value[operation])
                    $fatal(1,"unary result op%0d csr%h psw%h",operation,csr,psw);
                if(reads!=(operation==0?0:1) || writes!=(operation==7?0:1))$fatal(1,"unary I/O count op%0d",operation);
            end else begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(kind==3?2'd1:2'd2) || psw!==3 || csr!==(BYTE_VARIANT?16'ha581:16'h8001))
                    $fatal(1,"unary error commit op%0d kind%0d csr%h psw%h fault%0d",operation,kind,csr,psw,fault);
                if(reads!=(kind==3 || operation==0?0:1) || writes!=(kind==2?1:0))$fatal(1,"unary error bus count");
                if(observed_fault!=0 && dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            end
            cases=cases+1;
        end
        if(cases!=46)$fatal(1,"case count");
        if(BYTE_VARIANT)$display("PASS single byte directed: 46 I/O/read-error/write-error/odd-pointer cases, 0..3 waits");
        else $display("PASS single directed: 46 I/O/read-error/write-error/odd-address cases, 0..3 waits");
        $finish;
    end
    initial begin #1000000;$fatal(1,"single directed timeout");end
endmodule
