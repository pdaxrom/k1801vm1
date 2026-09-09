// CP16: failed-operation checks end at BUS_FAULT_ENTRY before vector traffic.
`timescale 1ns/1ps
// SWAB/SXT CSR effects and MARK pop failures, including held requests.
module tb_extra_faults;
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
    integer operation,kind,edge_case,waits,age=0,cycles,reads=0,writes=0,attempts=0,cases=0;
    reg pending=0;
    reg [33:0] held_request;
    wire ack=request && age==waits;
    wire error=addr!=0 && ((kind==1 && reading) || (kind==2 && writing));
    wire [15:0] rdata=addr==0 ? opcode : csr;
    reg [15:0] expected_value,initial_value,initial_psw;
    reg [3:0] expected_flags;
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
            if((addr[0] && !byte_access) || byte_access || reading==writing)$fatal(1,"invalid external request");
            if(ack)begin
                age<=0;pending<=0;attempts=attempts+1;
                if(addr!=0)begin
                    if(addr!==(operation==2?16'd8:16'o177562))$fatal(1,"unexpected operand address %o",addr);
                    if(reading)reads=reads+1;else writes=writes+1;
                    if(writing && !error)csr<=wdata;
                end
            end else begin
                pending<=1;held_request<={reading,writing,addr,wdata};age<=age+1;
            end
        end else if(pending)$fatal(1,"request dropped before ACK");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(operation=0;operation<3;operation=operation+1)
        for(edge_case=0;edge_case<4;edge_case=edge_case+1)
        for(kind=0;kind<4;kind=kind+1)
        if((operation<2 && !(operation==1 && kind==1)) ||
           (operation==2 && edge_case<2 && kind<2))begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=(operation+kind+edge_case)%4;
            case(edge_case)
                0:initial_value=16'h0080;
                1:initial_value=16'h80ff;
                2:initial_value=16'h7f00;
                3:initial_value=16'hff80;
            endcase
            csr=initial_value;initial_psw=edge_case[0]?16'h000b:16'h0003;
            opcode=operation==0?16'o000311:operation==1?16'o006711:16'o006403;
            dut.engine.dp.rf.words[1]=kind==3?16'o177563:16'o177562;
            dut.engine.dp.rf.words[5]=16'h0200+{15'b0,edge_case[0]};
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.status.psw=initial_psw;
            expected_value=operation==0?{initial_value[7:0],initial_value[15:8]}:
                           initial_psw[3]?16'hffff:16'h0000;
            expected_flags=operation==0?{initial_value[15],initial_value[15:8]==0,2'b0}:
                           {initial_psw[3],!initial_psw[3],1'b0,initial_psw[0]};
            cycles=0;while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(operation==2)begin
                if(reads!=1 || writes!=0 || psw!==initial_psw || csr!==initial_value ||
                   dut.engine.dp.rf.words[7]!==16'h0200+{15'b0,edge_case[0]})$fatal(1,"MARK bus/PSW/PC");
                if(kind==0)begin
                    if(stopped || !retire || fault!=0 || dut.engine.dp.rf.words[6]!==16'd10 ||
                       dut.engine.dp.rf.words[5]!==initial_value)$fatal(1,"MARK result");
                end else begin
                    if(stopped || upc!==10'h015 || retire || observed_fault!=2 || dut.engine.dp.rf.words[6]!==16'd8 ||
                       dut.engine.dp.rf.words[5]!==16'h0200+{15'b0,edge_case[0]})$fatal(1,"MARK failed pop");
                end
            end else if(kind==0)begin
                if(stopped || !retire || fault!=0 || psw!=={12'b0,expected_flags} || csr!==expected_value)
                    $fatal(1,"extra result op%0d edge%0d csr%h psw%h",operation,edge_case,csr,psw);
                if(reads!=(operation==0?1:0) || writes!=1)$fatal(1,"SWAB/SXT I/O count");
            end else begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(kind==3?2'd1:2'd2) || psw!==initial_psw || csr!==initial_value)
                    $fatal(1,"extra failed commit op%0d kind%0d",operation,kind);
                if(reads!=(kind==3 || operation==1?0:1) || writes!=(kind==2?1:0))$fatal(1,"extra error bus count");
            end
                if(observed_fault!=0 && dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            cases=cases+1;
        end
        // Four ordinary-oracle exclusions: @-(PC) reads its own odd opcode
        // as a pointer. Reject the final odd word before an external request.
        for(operation=0;operation<2;operation=operation+1)
        for(kind=0;kind<2;kind=kind+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);waits=2*operation+kind;csr=16'h80ff;
            initial_psw=16'(8+2*operation+kind);dut.engine.status.psw=initial_psw;
            opcode=operation==0?16'o000357:16'o006757;
            cycles=0;while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(stopped || upc!==10'h015 || retire || observed_fault!=1 || psw!==initial_psw || attempts!=2 ||
               reads!=0 || writes!=0 || csr!==16'h80ff || dut.engine.dp.rf.words[7]!==0)
                $fatal(1,"extra @-(PC) odd pointer handling");
                if(observed_fault!=0 && dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            cases=cases+1;
        end
        if(cases!=36)$fatal(1,"case count %0d",cases);
        $display("PASS extra directed: 36 SWAB/SXT CSR/flags/fault, MARK pop and @-(PC) cases, 0..3 waits");$finish;
    end
    initial begin #1000000;$fatal(1,"extra directed timeout");end
endmodule
