// CP16: failed-operation checks end at BUS_FAULT_ENTRY before vector traffic.
`timescale 1ns/1ps
module tb_ea_faults;
    reg clk=0,reset=1,inject_error=0;
    reg [15:0] opcode=0,source_value=0,destination_value=0,csr=0;
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
    wire ack=request;
    wire error=inject_error && writing && addr==16'h4000;
    wire io_page=&addr[15:13];
    wire [15:0] rdata=addr==0 ? opcode : addr==16'h2000 ? source_value :
                     addr==16'h4000 ? destination_value : io_page ? csr : 16'b0;
    integer c,cycles,reads=0,writes=0,io_reads=0,io_writes=0,attempts=0;
    reg [15:0] last_write;
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
        if(reset)begin reads=0;writes=0;io_reads=0;io_writes=0;attempts=0;last_write=0;end
        else if(request && ack)begin
            attempts=attempts+1;
            if(addr[0] || byte_access)$fatal(1,"odd/byte word escaped core");
            if(!error)begin
                if(writing)begin
                    writes=writes+1;last_write=wdata;
                    if(io_page)io_writes=io_writes+1;
                    if(addr==16'h4000)destination_value<=wdata;
                end else begin reads=reads+1;if(io_page)io_reads=io_reads+1;end
            end
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(c=0;c<14;c=c+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            dut.engine.dp.rf.words[0]=16'ha5a5;dut.engine.dp.rf.words[1]=16'o177562;
            dut.engine.status.psw=3;inject_error=0;source_value=0;destination_value=1;csr=16'h2222;
            case(c)
                0:opcode=16'o010011; // MOV R0,(R1): no destination read
                1:begin opcode=16'o020011;dut.engine.dp.rf.words[0]=16'h1111;end
                2:begin opcode=16'o060011;dut.engine.dp.rf.words[0]=16'h7fff;csr=1;end
                3:begin opcode=16'o011001;dut.engine.dp.rf.words[0]=16'h2001;end
                4:begin opcode=16'o013001;dut.engine.dp.rf.words[0]=16'h2000;source_value=16'h4001;end
                5,6,11,12,13:begin
                    case(c)
                        5:opcode=16'o010011;
                        6:opcode=16'o060011;
                        11:opcode=16'o040011;
                        12:opcode=16'o050011;
                        13:opcode=16'o160011;
                    endcase
                    dut.engine.dp.rf.words[0]=16'h7fff;dut.engine.dp.rf.words[1]=16'h4000;inject_error=1;end
                7:opcode=16'o030011; // BIT: exactly one read, no write
                8:opcode=16'o040011;
                9:opcode=16'o050011;
                10:begin opcode=16'o160011;dut.engine.dp.rf.words[0]=1;csr=16'h8000;end
            endcase
            cycles=0;while(!retire && !stopped && (observed_fault==0 || dut.engine.fault_repair) && cycles<100)begin tick;cycles=cycles+1;end
            if(c<3 || (c>=7 && c<=10))begin
                if(!retire || stopped || io_reads!=(c==0?0:1) || io_writes!=((c==1 || c==7)?0:1))
                    $fatal(1,"EA I/O side effects case%0d",c);
                if(c==0 && (last_write!==16'ha5a5 || psw!==9))$fatal(1,"MOV I/O result");
                if(c==1 && psw!==9)$fatal(1,"CMP I/O flags");
                if(c==2 && (last_write!==16'h8000 || psw!==10))$fatal(1,"ADD I/O result");
                if(c==7 && psw!==1)$fatal(1,"BIT I/O flags/C preserve");
                if(c==8 && (last_write!==16'h0202 || psw!==1))$fatal(1,"BIC I/O result/C preserve");
                if(c==9 && (last_write!==16'ha7a7 || psw!==9))$fatal(1,"BIS I/O result/C preserve");
                if(c==10 && (last_write!==16'h7fff || psw!==2))$fatal(1,"SUB I/O result/overflow");
            end else begin
                if(stopped || upc!==10'h015 || retire || observed_fault!==(c<5?2'd1:2'd2) || psw!==3 || writes!=0)
                    $fatal(1,"EA fault commit case%0d psw%h fault%h",c,psw,fault);
                if(c==3 && attempts!=1)$fatal(1,"odd source bus activity");
                if(c==4 && (attempts!=2 || dut.engine.dp.rf.words[0]!==16'h2002))$fatal(1,"odd deferred source");
                if(c>=5 && destination_value!==1)$fatal(1,"failed store committed");
                if(observed_fault!=0 && dut.engine.seq.link_valid)$fatal(1,"fault left EA CALL link live");
            end
        end
        $display("PASS EA directed: 14 cases, seven word operations demand I/O, odd source/pointer, five store errors preserve flags and memory");
        $finish;
    end
endmodule
