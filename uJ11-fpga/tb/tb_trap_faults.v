`timescale 1ns/1ps
// Each vector/stack transfer can fail; diagnostic STOP is the current double-fault policy.
module tb_trap_faults;
    reg clk=0,reset=1,pending=0;
    reg [15:0] opcode,vector_address,new_pc,initial_sp,pushed_psw,pushed_pc;
    wire [15:0] addr,wdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer operation,kind,waits,age=0,beat=0,cycles,cases=0,fail_at,k,expected_beats;
    reg [34:0] held;
    wire ack=request && age==waits;
    wire error=ack && beat==fail_at;
    wire [15:0] rdata=addr==0 ? opcode:addr==vector_address+16'd2 ? 16'h00a9:
        addr==vector_address ? new_pc:addr==16'h6000 ? new_pc:addr==16'h6002 ? 16'h00a9:16'hdead;
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
        if(reset)begin beat<=0;age<=0;pending<=0;pushed_psw<=16'hdead;pushed_pc<=16'hdead;end
        else if(request)begin
            if(byte_access || addr[0] || reading==writing)$fatal(1,"non-word trap request");
            if(pending && {reading,writing,byte_access,addr,wdata}!==held)$fatal(1,"trap request changed before ACK");
            if(ack)begin
                beat<=beat+1;age<=0;pending<=0;
                if(beat==0)begin if(!reading || addr!=0)$fatal(1,"trap opcode beat");end
                else if(operation<4)begin
                    case(beat)
                        1:if(!reading || addr!==vector_address+16'd2)$fatal(1,"vector PSW must be read first");
                        2:if(!reading || addr!==vector_address)$fatal(1,"vector PC beat");
                        3:begin
                            if(!writing || addr!==initial_sp-16'd2 || wdata!==16'h0043)$fatal(1,"push old PSW");
                            if(!error)pushed_psw<=wdata;
                        end
                        4:begin
                            if(!writing || addr!==initial_sp-16'd4 || wdata!==16'd2)$fatal(1,"push old PC");
                            if(!error)pushed_pc<=wdata;
                        end
                        default:$fatal(1,"unexpected trap transfer");
                    endcase
                end else begin
                    if(!reading || (beat==1 && addr!==initial_sp) ||
                       (beat==2 && addr!==initial_sp+16'd2) || beat>2)$fatal(1,"RTI transfer order");
                end
            end else begin
                held<={reading,writing,byte_access,addr,wdata};pending<=1;age<=age+1;
            end
        end else if(pending)$fatal(1,"trap request dropped before ACK");
    end
    task tick;begin @(posedge clk);#1;end endtask
    initial begin
        #100;
        for(operation=0;operation<5;operation=operation+1)
        for(kind=0;kind<6;kind=kind+1)
        for(waits=0;waits<4;waits=waits+1)
        if(operation<4 || kind<3 || kind==5)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            case(operation)
                0:begin opcode=16'o000003;vector_address=16'o14;end
                1:begin opcode=16'o000004;vector_address=16'o20;end
                2:begin opcode=16'o104377;vector_address=16'o30;end
                3:begin opcode=16'o104777;vector_address=16'o34;end
                4:begin opcode=16'o000002;vector_address=0;end
            endcase
            fail_at=(kind==0 || kind==5) ? -1:kind;
            initial_sp=kind==5 ? 16'h6001:16'h6000;new_pc=16'h3000+16'(waits&1);
            for(k=0;k<6;k=k+1)dut.engine.dp.rf.words[k]=16'h1000+16'(k);
            dut.engine.dp.rf.words[6]=initial_sp;dut.engine.status.psw=16'h0043;
            cycles=0;while(!retire && !stopped && upc!=10'h015 && cycles<250)begin tick;cycles=cycles+1;end
            for(k=0;k<6;k=k+1)if(dut.engine.dp.rf.words[k]!==16'h1000+16'(k))$fatal(1,"trap clobbered R0..R5");
            if(kind==0)begin
                expected_beats=operation<4 ? 5:3;
                if(!retire || stopped || fault!=0 || psw!==16'h00a9 || dut.engine.dp.rf.words[7]!==new_pc ||
                   dut.engine.dp.rf.words[6]!==(operation<4 ? 16'h5ffc:16'h6004))$fatal(1,"trap/RTI success");
            end else begin
                expected_beats=kind==5 ? (operation<4 ? 3:1):kind+1;
                if(retire || (operation<4 ? (!stopped || fault!==(kind==5 ? 2'd1:2'd2)) : (stopped || upc!==10'h015 || fault!=0)) || dut.engine.dp.rf.words[7]!==16'd2)
                    $fatal(1,"trap/RTI failed transfer outcome");
                if(operation<4)begin
                    if(psw!==(kind<=2 ? 16'h0043:16'h00a9) || dut.engine.dp.rf.words[6]!==
                       (kind<=2 ? initial_sp:kind==4 ? initial_sp-16'd4:initial_sp-16'd2))$fatal(1,"trap partial state");
                end else if(psw!==16'h0043 || dut.engine.dp.rf.words[6]!==(kind==2 ? initial_sp+16'd2:initial_sp))
                    $fatal(1,"RTI partial state");
            end
            if(beat!=expected_beats)$fatal(1,"trap beat count");
            if(pushed_psw!==(operation<4 && (kind==0 || kind==4) ? 16'h0043:16'hdead) ||
               pushed_pc!==(operation<4 && kind==0 ? 16'd2:16'hdead))$fatal(1,"failed store changed stack");
            if(stopped)begin cycles=beat;repeat(4)tick;if(cycles!=beat)$fatal(1,"stopped trap bus active");end
            cases=cases+1;
        end
        if(cases!=112)$fatal(1,"trap directed count");
        $display("PASS trap directed: 112 vector/stack/RTI success, ACK error and odd-SP cases, 0..3 waits");$finish;
    end
    initial begin #3000000;$fatal(1,"trap directed timeout");end
endmodule
