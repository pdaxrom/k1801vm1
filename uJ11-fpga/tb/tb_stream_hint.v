`timescale 1ns/1ps
module tb_stream_hint;
    reg [35:0] uword=0;
    reg [15:0] ir=0;
    wire stream;
    reg want;
    integer cmd,a,rs,rd,h,checks=0;
    uj11_stream dut(.*);
    initial begin
        for(cmd=0;cmd<16;cmd=cmd+1) for(a=0;a<32;a=a+1)
        for(rs=0;rs<8;rs=rs+1) for(rd=0;rd<8;rd=rd+1) for(h=0;h<2;h=h+1) begin
            uword={1'b1,cmd[3:0],a[4:0],26'b0}; uword[5]=h[0];
            ir=0; ir[8:6]=rs[2:0]; ir[2:0]=rd[2:0];
            want=cmd==2 || (cmd==11 && h && (a==7 || (a==16 && rs==7) || (a==17 && rd==7)));
            #1; if(stream!==want) $fatal(1,"stream qualifier"); checks=checks+1;
            uword[35]=0; #1; if(stream) $fatal(1,"ALU word became a stream read");
        end
        $display("PASS stream hint: %0d control encodings and ALU rejection",checks);
        $finish;
    end
endmodule
