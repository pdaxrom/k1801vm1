// CP16: failed-operation checks end at BUS_FAULT_ENTRY before vector traffic.
`timescale 1ns/1ps
// XOR read/modify/write of a CSR whose successful read clears its contents.
module tb_xor_io;
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
                    if(addr!==16'o177562)$fatal(1,"unexpected operand address %o",addr);
                    if(reading)reads=reads+1;else writes=writes+1;
                    if(!error)csr<=writing ? wdata : 16'b0;
                end
            end else begin
                pending<=1;held_request<={reading,writing,addr,wdata};age<=age+1;
            end
        end else if(pending)$fatal(1,"request dropped before ACK");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(edge_case=0;edge_case<4;edge_case=edge_case+1)
        for(operation=0;operation<16;operation=operation+1)
        for(kind=0;kind<4;kind=kind+1)
        for(waits=0;waits<4;waits=waits+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            case(edge_case)
                0:begin initial_value=16'hffff;expected_value=16'h0000;end
                1:begin initial_value=16'hffff;expected_value=16'h8000;end
                2:begin initial_value=16'h8000;expected_value=16'h0000;end
                3:begin initial_value=16'h5555;expected_value=16'haaaa;end
            endcase
            csr=initial_value;initial_psw=16'ha0|16'(operation);
            opcode=16'o074011;
            dut.engine.dp.rf.words[0]=initial_value^expected_value;
            dut.engine.dp.rf.words[1]=kind==3?16'o177563:16'o177562;
            dut.engine.dp.rf.words[6]=16'h6000;dut.engine.status.psw=initial_psw;
            expected_flags={expected_value[15],expected_value==0,1'b0,initial_psw[0]};
            cycles=0;
            while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<200)begin tick;cycles=cycles+1;end
            if(kind==0)begin
                if(stopped || !retire || fault!=0 || psw!=={12'h00a,expected_flags} || csr!==expected_value || reads!=1 || writes!=1)
                    $fatal(1,"XOR CSR success edge%0d flags%h reads/writes%0d/%0d csr%h psw%h",edge_case,initial_psw,reads,writes,csr,psw);
            end else begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(kind==3?2'd1:2'd2) || psw!==initial_psw ||
                   csr!==(kind==2?16'b0:initial_value) || reads!=(kind==3?0:1) || writes!=(kind==2?1:0))
                    $fatal(1,"XOR CSR fault kind%0d flags%h reads/writes%0d/%0d csr%h psw%h",kind,initial_psw,reads,writes,csr,psw);
                if(dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            end
            if(dut.engine.dp.rf.words[0]!==(initial_value^expected_value) || dut.engine.dp.rf.words[7]!==2 ||
               dut.engine.dp.rf.words[6]!==16'h6000 || dut.engine.dp.rf.words[1]!==(kind==3?16'o177563:16'o177562))$fatal(1,"XOR unexpected register write");
            cases=cases+1;
        end
        if(cases!=1024)$fatal(1,"XOR CSR case count %0d",cases);
        $display("PASS XOR CSR: 1024 read-clear/write/flags/error/odd-address cases, all NZVC and 0..3 waits");$finish;
    end
    initial begin #10000000;$fatal(1,"XOR CSR timeout");end
endmodule
