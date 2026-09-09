`timescale 1ns/1ps
module tb_bench;
    reg clk=0,reset=1;
    reg [7:0] wait_states=0;
    wire [15:0] addr,wdata,rdata,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,ack,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [31:0] transactions,writes;
    reg [31:0] rng=32'h00111801;
    integer file,w,m,k,n,clocks,start_transactions,expected_waits,wait_sum;
    reg [15:0] opcode;
    string names[0:8];
    uj11_core dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(wdata),
        .mem_request(request),.mem_read(reading),.mem_write(writing),.mem_byte(byte_access),
        .mem_ack(ack),.mem_error(1'b0),.mem_read_data(rdata),.stopped(stopped),
        .fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(reading),.writing(writing),
        .byte_access(byte_access),.addr(addr),.write_data(wdata),.wait_states(wait_states),
        .ack(ack),.read_data(rdata),.transactions(transactions),.writes(writes));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    task tick; begin @(posedge clk); #1; end endtask
    task new_latency;
        begin
            rng=rng*32'd1664525+32'd1013904223;
            wait_states = m==0 ? 0 : m==1 ? 2 : {6'b0,rng[25:24]};
        end
    endtask
    initial begin
        names[0]="MOV_RR_loop"; names[1]="ADD_RR_loop"; names[2]="CMP_RR_loop";
        names[3]="mixed_RR_loop"; names[4]="BR_self";
        names[5]="BIT_RR_loop";names[6]="BIC_RR_loop";names[7]="BIS_RR_loop";names[8]="SUB_RR_loop";
        file=$fopen("build/benchmarks.json","w");
        if(!file) $fatal(1,"cannot write benchmark results");
        $fwrite(file,"[\n");
        #100;
        for(w=0;w<9;w=w+1) for(m=0;m<3;m=m+1) begin
            @(negedge clk); reset=1; wait_states=0;
            tick; @(negedge clk); reset=0;
            repeat(17) tick;
            @(negedge clk);
            for(k=0;k<7;k=k+1) dut.engine.dp.rf.words[k]=k;
            for(k=0;k<64;k=k+1) begin
                case(w)
                    0: opcode=16'o010102; // MOV R1,R2
                    1: opcode=16'o060102; // ADD R1,R2
                    2: opcode=16'o020102; // CMP R1,R2
                    3: case(k%4)
                        0: opcode=16'o010102;
                        1: opcode=16'o060302; // ADD R3,R2
                        2: opcode=16'o020102;
                        3: opcode=16'o060401; // ADD R4,R1
                    endcase
                    5: opcode=16'o030102;
                    6: opcode=16'o040102;
                    7: opcode=16'o050102;
                    8: opcode=16'o160102;
                    4: opcode=16'o000777; // BR .
                endcase
                if(k==63 && w!=4) opcode=16'o000700; // BR to first word
                ram.bytes[k*2]=opcode[7:0]; ram.bytes[k*2+1]=opcode[15:8];
            end
            new_latency;
            n=0;
            while(n<64) begin
                tick;
                if(stopped) $fatal(1,"benchmark warmup stop");
                if(retire) begin n=n+1; new_latency; end
            end
            start_transactions=transactions;
            clocks=0; n=0; expected_waits=0; wait_sum=0;
            while(n<4096) begin
                tick; clocks=clocks+1;
                if(stopped || clocks>40000) $fatal(1,"benchmark stop/timeout");
                if(retire) begin
                    n=n+1; wait_sum=wait_sum+wait_states;
                    new_latency;
                end
            end
            expected_waits=clocks-2*n;
            if(transactions-start_transactions!=n || writes || expected_waits!=wait_sum)
                $fatal(1,"benchmark bus/clock accounting");
            $fwrite(file,"  {\"workload\":\"%s\",\"wait_mode\":%0d,\"instructions\":%0d,\"microclocks\":%0d,\"memory_cycles\":%0d,\"wait_clocks\":%0d}%s\n",
                    names[w],m,n,clocks,transactions-start_transactions,wait_sum,(w==8 && m==2) ? "" : ",");
            $display("BENCH %s wait=%0d: %0d clocks / %0d instructions, %0d memory cycles",names[w],m,clocks,n,transactions-start_transactions);
        end
        $fwrite(file,"]\n"); $fclose(file);
        $display("PASS benchmarks: 27 runs, 4096 measured instructions/run, warmup excluded");
        $finish;
    end
    initial begin #10000000; $fatal(1,"benchmark timeout"); end
endmodule
