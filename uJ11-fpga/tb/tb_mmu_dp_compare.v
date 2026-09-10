`timescale 1ns/1ps
module tb_mmu_dp_compare;
    parameter integer FULL=1;
    parameter integer CANDIDATE=1;
    reg clk=0,reset=1,enable=0,carry=0,byte_mode=0,borrow=0,check_length=0;
    reg [3:0] a=0,b=0,operation=0;
    reg [2:0] pair=0,destination=0;
    reg [15:0] d=0,apr_data=0;
    wire [101:0] gold,actual;
    wire [15:0] ga,gb,gq,gr,gw,gblock,aa,ab,aq,ar,aw,ablock;
    wire [3:0] gf,af;
    wire gwrite,awrite,gerror,aerror;
    reg [63:0] random_state=64'h6928050e77d9a413;
    reg [15:0] saved_q;
    integer i,j,checks=0;
    always #5 clk=~clk;
    uj11_mmu_dp_compare #(.SHARED(0)) reference_dp(.clk(clk),.reset(reset),.enable(enable),
        .carry(carry),.byte_mode(byte_mode),.borrow(borrow),.check_length(check_length),
        .a(a),.b(b),.operation(operation),.pair(pair),.destination(destination),.d(d),.apr_data(apr_data),
        .read_a(ga),.read_b(gb),.q(gq),.result(gr),.writeback(gw),.block_address(gblock),
        .nzvc(gf),.rf_write(gwrite),.length_error(gerror));
    uj11_mmu_dp_compare #(.SHARED(CANDIDATE)) candidate_dp(.clk(clk),.reset(reset),.enable(enable),
        .carry(carry),.byte_mode(byte_mode),.borrow(borrow),.check_length(check_length),
        .a(a),.b(b),.operation(operation),.pair(pair),.destination(destination),.d(d),.apr_data(apr_data),
        .read_a(aa),.read_b(ab),.q(aq),.result(ar),.writeback(aw),.block_address(ablock),
        .nzvc(af),.rf_write(awrite),.length_error(aerror));
    assign gold={ga,gb,gq,gr,gw,gf,gwrite,gblock,gerror};
    assign actual={aa,ab,aq,ar,aw,af,awrite,ablock,aerror};
    task cycle;
        begin
            #1;
            if(actual!==gold)$fatal(1,"MMU datapath pre-edge mismatch check%0d borrow%b length%b apr%h op%0d pair%0d dst%0d",checks,borrow,check_length,apr_data,operation,pair,destination);
            saved_q=aq;
            if(borrow && awrite)$fatal(1,"borrow RF write");
            @(posedge clk);#1;
            if(actual!==gold)$fatal(1,"MMU datapath post-edge mismatch check%0d",checks);
            if(borrow && !reset && aq!==saved_q)$fatal(1,"borrow Q write");
            checks=checks+1;
            @(negedge clk);
        end
    endtask
    task load(input [3:0] regno,input [15:0] value);
        begin
            borrow=0;reset=0;enable=1;operation=0;pair=6;destination=1;
            b=regno;d=value;cycle();
        end
    endtask
    initial begin
        cycle();reset=0;
        for(i=0;i<16;i=i+1)load(i[3:0],16'h531b^(i[15:0]*16'h1627));
        // One ordinary Q value, then complete relocation and PDR arithmetic
        // coverage. High VA/page/byte-offset bits must not enter either sum.
        destination=2;d=16'had35;cycle();
        for(i=0;i<128;i=i+1)begin
            load(0,{i[2:0],i[6:0],i[5:0]});a=0;
            for(j=0;j<65536;j=j+(FULL!=0 ? 1 : 257))begin
                apr_data=j[15:0];borrow=1;enable=1;carry=j[0];byte_mode=j[1];
                pair=j[4:2];operation=j[8:5];destination=j[11:9];b=j[15:12];
                check_length=0;cycle();check_length=1;cycle();
            end
        end
        // Mixed normal RF/Q writes and borrower holds under arbitrary inputs.
        // Every opcode, pair, destination, byte/word and carry is exercised.
        for(i=0;i<200000;i=i+1)begin
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>7);
            random_state=random_state^(random_state<<17);
            {apr_data,check_length,borrow,byte_mode,enable,carry,d,destination,pair,operation,b,a}=random_state[54:0];
            reset=(i%1000)==999;cycle();
        end
        reset=0;
        // Unused CPU controls may be X during a borrowed word; the MMU
        // result and RF/Q hold must remain deterministic in four-state runs.
        load(0,16'h1fc0);a=0;b=7;borrow=1;apr_data=16'hffff;
        operation=4'bx;pair=3'bx;destination=3'bx;d=16'bx;carry=1'bx;byte_mode=1'bx;
        check_length=0;cycle();check_length=1;cycle();
        // Re-read every entry after borrowing to expose any hidden corruption.
        borrow=0;enable=0;operation=0;pair=0;destination=0;carry=0;byte_mode=0;d=0;
        for(i=0;i<16;i=i+1)begin a=i[3:0];b=~i[3:0];cycle();end
        $display("PASS MMU datapath sharing: %0d cycles FULL=%0d CANDIDATE=%0d; arithmetic, ordinary RF/Q writes, borrow hold, reset, four-state unused controls",checks,FULL,CANDIDATE);
        $finish;
    end
endmodule
