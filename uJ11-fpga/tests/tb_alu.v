`timescale 1ns/1ps
module tb_alu;
    reg [15:0] a,b;
    reg [3:0] operation;
    reg carry;
    wire [15:0] result;
    wire [3:0] nzvc;
    reg [15:0] edges[0:8];
    integer op,i,j,k,checks=0,seed=32'h1801;
    integer wide,signed_wide;
    reg [15:0] expected;
    reg ec,ev;
    wire byte_mode=1'b0;
    uj11_alu dut(.*);
    function integer signed16;
        input [15:0] value;
        begin signed16=value[15] ? {16'b0,value}-65536 : {16'b0,value}; end
    endfunction
    task verify;
        begin
            ec=0; ev=0; expected=a; wide=0; signed_wide=0;
            case(operation)
                0: expected=a;
                1: expected=b;
                2,3: begin
                    wide={16'b0,a}+{16'b0,b}+((operation==3 && carry) ? 1 : 0);
                    signed_wide=signed16(a)+signed16(b)+((operation==3 && carry) ? 1 : 0);
                    expected=wide[15:0]; ec=wide>65535; ev=signed_wide>32767 || signed_wide < -32768;
                end
                4,5: begin
                    wide={16'b0,a}-{16'b0,b}-((operation==5 && carry) ? 1 : 0);
                    signed_wide=signed16(a)-signed16(b)-((operation==5 && carry) ? 1 : 0);
                    expected=wide[15:0]; ec=wide<0; ev=signed_wide>32767 || signed_wide < -32768;
                end
                6: expected=a&b;
                7: expected=a|b;
                8: expected=a^b;
                9: expected=a&~b;
                10: begin expected=~a; ec=1; end
                11: begin expected=a<<1; ec=a[15]; ev=expected[15]^ec; end
                12: begin expected=a>>1; ec=a[0]; ev=expected[15]^ec; end
                13: begin expected=(a>>1) | (a&16'h8000); ec=a[0]; ev=expected[15]^ec; end
                14: begin expected=(a<<1) | {15'b0,carry}; ec=a[15]; ev=expected[15]^ec; end
                15: begin expected=(a>>1) | (carry ? 16'h8000 : 16'h0); ec=a[0]; ev=expected[15]^ec; end
            endcase
            #1; checks=checks+1;
            if(result!==expected || nzvc!=={expected[15],expected==16'b0,ev,ec})
                $fatal(1,"ALU op=%d a=%h b=%h c=%b -> %h/%h expected %h/%b%b%b%b",operation,a,b,carry,result,nzvc,expected,expected[15],expected==16'b0,ev,ec);
        end
    endtask
    initial begin
        edges[0]=0; edges[1]=1; edges[2]=2; edges[3]='h7ffe; edges[4]='h7fff;
        edges[5]='h8000; edges[6]='h8001; edges[7]='hfffe; edges[8]='hffff;
        for(op=0;op<16;op=op+1) begin
            operation=op[3:0];
            for(k=0;k<2;k=k+1) begin
                carry=k[0];
                for(i=0;i<9;i=i+1) for(j=0;j<9;j=j+1) begin a=edges[i]; b=edges[j]; verify; end
                for(i=0;i<2000;i=i+1) begin a=$random(seed); b=$random(seed); verify; end
            end
        end
        $display("PASS ALU: %0d independent result/NZVC checks",checks); $finish;
    end
endmodule
