`timescale 1ns/1ps
module tb_oddrxe_cp56;
    reg clk=0,rst=1,d0=0,d1=0;
    wire gold,gate;
    integer i,checks=0;
    reg [31:0] random_state=32'h41b38572;
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
    ODDRXE vendor(d0,d1,rst,clk,gold);
    ODDRXE_portable portable(d0,d1,rst,clk,gate);
    always #16.915 clk=~clk;
    initial begin
        #300;rst=0;
        for(i=0;i<10000;i=i+1)begin
            #4.037;
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            {d0,d1}=random_state[1:0];rst=random_state[6:2]==0;
        end
        #100;
        $display("PASS CP56 ODDRXE portable/vendor: %0d half-cycle and reset observations",checks);
        $finish;
    end
    always @(clk or rst)begin
        #0.01;
        if($time>300)begin
            if(gold!==gate)$fatal(1,"ODDRXE model mismatch");
            checks=checks+1;
        end
    end
endmodule
