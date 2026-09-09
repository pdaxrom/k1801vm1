`timescale 1ns/1ps
module tb_fp_events;
    reg clk=0,reset=1,irq=0;
    wire request,rd,wr,byte_access,ack,retire,stopped,irq_ack;
    wire [15:0] address,wdata,rdata,psw;
    wire [9:0] upc;
    wire [1:0] fault;
    wire [31:0] transactions,writes;
    integer op,events,cc,checks=0,guard,irq_count=0,k;
    reg [15:0] opcode,expected_psw,expected_r2;
    reg [15:0] before_rf[0:7];
    always #5 clk=~clk;
    always @(posedge clk)if(irq_ack)irq_count=irq_count+1;
    uj11_core #(.ROM_DECODE(1),.FP11_CONTROL(1)) dut(.clk(clk),.reset(reset),
        .irq_valid(irq),.irq_priority(3'd4),.irq_vector(8'o30),.irq_ack(irq_ack),.waiting(),.peripheral_reset(),
        .mem_addr(address),.mem_write_data(wdata),.mem_request(request),.mem_read(rd),.mem_write(wr),
        .mem_byte(byte_access),.mem_ack(ack),.mem_error(1'b0),.mem_read_data(rdata),
        .stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(),
        .ir(),.mdr(),.psw(psw),.q(),.debug_rf_write(),.debug_rf_address(),.debug_rf_data());
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(rd),.writing(wr),.byte_access(byte_access),
        .addr(address),.write_data(wdata),.wait_states(8'd2),.ack(ack),.read_data(rdata),.transactions(transactions),.writes(writes));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    task word_at(input [15:0] a,v);begin ram.bytes[a]=v[7:0];ram.bytes[a+16'd1]=v[15:8];end endtask
    function [15:0] word_read(input [15:0] a);begin word_read={ram.bytes[a+16'd1],ram.bytes[a]};end endfunction
    task execute;
        begin: instruction
            for(guard=0;guard<50;guard=guard+1)begin
                @(posedge clk);#1;
                if(stopped)$fatal(1,"unexpected stop");
                if(retire)disable instruction;
            end
            $fatal(1,"retire timeout");
        end
    endtask
    initial begin
        #100;
        for(op=0;op<7;op=op+1)for(events=0;events<4;events=events+1)for(cc=0;cc<16;cc=cc+1)begin
            @(negedge clk);reset=1;irq=0;
            @(posedge clk);#1;@(negedge clk);reset=0;
            repeat(17)begin @(posedge clk);#1;end
            // FPS must be zero again after reset, including after a nonzero write.
            @(negedge clk);dut.engine.dp.rf.words[7]=16'o400;word_at(16'o400,16'o170202);
            execute;
            if(dut.engine.dp.rf.words[2]!==0)$fatal(1,"FPS reset did not clear prior state");
            @(negedge clk);dut.engine.dp.rf.words[0]=16'hcfef;
            dut.engine.dp.rf.words[7]=16'o400;word_at(16'o400,16'o170100);
            execute;
            @(negedge clk);
            case(op)
                0:opcode=16'o170000;1:opcode=16'o170001;2:opcode=16'o170002;
                3:opcode=16'o170011;4:opcode=16'o170012;5:opcode=16'o170102;default:opcode=16'o170202;
            endcase
            for(k=0;k<6;k=k+1)begin before_rf[k]=16'h1230+k[15:0];dut.engine.dp.rf.words[k]=before_rf[k];end
            before_rf[6]=16'o1400;before_rf[7]=16'o1000;
            dut.engine.dp.rf.words[6]=before_rf[6];dut.engine.dp.rf.words[7]=before_rf[7];
            dut.engine.status.psw=(events[0]?16'h10:16'h0)|cc[15:0];
            expected_psw=(events[0]?16'h10:16'h0)|(op==0?16'hf:cc[15:0]);
            expected_r2=op==6?16'hcfef:before_rf[2];
            word_at(16'o1000,opcode);word_at(16'o14,16'o3000);word_at(16'o16,16'o340);
            word_at(16'o60,16'o2000);word_at(16'o62,16'o340);
            irq_count=0;irq=events[1];
            execute;
            guard=0;
            while(upc!=10'h020 && guard<50)begin @(posedge clk);#1;guard=guard+1;end
            if(upc!=10'h020 || stopped)$fatal(1,"trap frame timeout");
            if(dut.engine.dp.rf.words[2]!==expected_r2)$fatal(1,"FP destination lost at trap");
            for(k=0;k<6;k=k+1)if(k!=2 && dut.engine.dp.rf.words[k]!==before_rf[k])$fatal(1,"register clobber at trap");
            if(events!=0)begin
                if(dut.engine.dp.rf.words[7] !== (events[0]?16'o3000:16'o2000) || psw!==16'o340 ||
                   dut.engine.dp.rf.words[6]!==16'o1374 || word_read(16'o1374)!==16'o1002 ||
                   word_read(16'o1376)!==expected_psw)$fatal(1,"wrong FP boundary trap/frame op%o events%0d",opcode,events);
                if(irq_count !== (events==2?1:0))$fatal(1,"trace/IRQ priority");
            end else if(psw!==expected_psw || dut.engine.dp.rf.words[7]!==16'o1002 ||
                         dut.engine.dp.rf.words[6]!==before_rf[6] || irq_count!=0)$fatal(1,"ordinary boundary");
            checks=checks+1;
        end
        $display("PASS FP reset/trace/IRQ: %0d cases, seven controls, all NZVC, trace wins over IRQ, post-FP PC/PSW frame",checks);$finish;
    end
    initial begin #2000000;$fatal(1,"watchdog");end
endmodule
