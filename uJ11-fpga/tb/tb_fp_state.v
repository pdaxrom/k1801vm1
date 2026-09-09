`timescale 1ns/1ps
module tb_fp_state;
    reg clk=0,enable=0,fp_enable=0,fp_write=0;
    reg [15:0] incoming=0,fp_wdata=0;
    reg [4:0] fp_address=0;
    wire [15:0] fp_rdata;
    wire [9:0] entry,gold;
    reg [15:0] saved[0:31];
    integer i,j,checks=0;
    always #5 clk=~clk;
    uj11_decode_rom #(.FP11_CONTROL(1)) dut(.clk(clk),.enable(enable),.incoming(incoming),.entry(entry),
        .fp_enable(fp_enable),.fp_write(fp_write),.fp_address(fp_address),.fp_wdata(fp_wdata),.fp_rdata(fp_rdata));
    uj11_decode #(.FP11_CONTROL(1)) decode(incoming,gold);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task read_state;
        input integer a;
        begin
            @(negedge clk);enable=0;fp_enable=1;fp_write=0;fp_address=a[4:0];
            @(posedge clk);#1;
            if(fp_rdata!==saved[a])$fatal(1,"FP word %0d got %h expected %h",a,fp_rdata,saved[a]);
            checks=checks+1;
        end
    endtask
    initial begin
        #25;
        for(i=0;i<32;i=i+1)begin saved[i]=0;read_state(i);end
        for(j=0;j<34;j=j+1)begin
            for(i=0;i<32;i=i+1)begin
                @(negedge clk);fp_enable=1;fp_write=1;fp_address=i[4:0];
                fp_wdata=j<16 ? (16'b1<<j) : j<32 ? ~(16'b1<<(j-16)) : (16'h81f3*i)^j;
                saved[i]=fp_wdata;
                @(posedge clk);#1;
            end
            for(i=31;i>=0;i=i-1)read_state(i);
        end
        // Writes to every FP word must leave every opcode mapping intact.
        for(i=0;i<65536;i=i+1)begin
            @(negedge clk);fp_enable=0;fp_write=0;enable=1;incoming=i[15:0];
            @(posedge clk);#1;
            if(entry!==gold)$fatal(1,"decode after FP writes %o got %h expected %h",incoming,entry,gold);
            checks=checks+1;
            @(negedge clk);enable=0;
            @(posedge clk);#1;
            if(entry!==gold)$fatal(1,"decode hold %o",incoming);
        end
        for(i=0;i<32;i=i+1)read_state(i);
        @(negedge clk);fp_enable=0;fp_address=0;fp_wdata=16'hffff;fp_write=1;
        repeat(4)begin @(posedge clk);#1;if(fp_rdata!==saved[31])$fatal(1,"FP disabled hold");end
        for(i=0;i<32;i=i+1)read_state(i);
        $display("PASS shared decode/FP EBR: %0d checks, 32 words, walking bits, all 65536 dispatch entries after writes",checks);
        $finish;
    end
    initial begin #4000000;$fatal(1,"timeout");end
endmodule
