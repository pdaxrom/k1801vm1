`timescale 1ns/1ps
module tb_alu_byte;
    reg [15:0] a,b;
    reg [3:0] operation;
    reg carry;
    wire byte_mode=1'b1;
    wire [15:0] result;
    wire [3:0] nzvc;
    integer op,i,j,k,checks=0,wide,signed_wide;
    reg [7:0] expected;
    reg ec,ev;
    uj11_alu dut(.*);
    function integer signed8;
        input [7:0] v;
        begin signed8=v[7]?{24'b0,v}-256:{24'b0,v};end
    endfunction
    initial begin
        // Exhaust all low-byte operand pairs and C. Upper bytes deliberately
        // vary independently of the arithmetic sign and carry boundary.
        for(op=0;op<16;op=op+1)for(k=0;k<2;k=k+1)
        for(i=0;i<256;i=i+1)for(j=0;j<256;j=j+1)begin
            operation=op;carry=k;a={(j[7:0]^8'h35),i[7:0]};b={(i[7:0]^8'ha7),j[7:0]};
            expected=i;ec=0;ev=0;
            case(op)
                0:expected=i;1:expected=j;
                2,3:begin
                    wide=i+j+(op==3?k:0);signed_wide=signed8(i)+signed8(j)+(op==3?k:0);
                    expected=wide;ec=wide>255;ev=signed_wide>127 || signed_wide< -128;
                end
                4,5:begin
                    wide=i-j-(op==5?k:0);signed_wide=signed8(i)-signed8(j)-(op==5?k:0);
                    expected=wide;ec=wide<0;ev=signed_wide>127 || signed_wide< -128;
                end
                6:expected=i&j;7:expected=i|j;8:expected=i^j;9:expected=i&~j;
                10:begin expected=~i;ec=1;end
                11:begin expected=i<<1;ec=i>127;end
                12:begin expected=i>>1;ec=i&1;end
                13:begin expected=(i>>1)|(i&128);ec=i&1;end
                14:begin expected=(i<<1)|k;ec=i>127;end
                15:begin expected=(i>>1)|(k<<7);ec=i&1;end
            endcase
            if(op>=11)ev=expected[7]^ec;
            #1;checks=checks+1;
            if(result[7:0]!==expected || nzvc!=={expected[7],expected==0,ev,ec})
                $fatal(1,"byte ALU op%0d A%h B%h C%b: %h/%h expected %h/%b%b%b%b",op,a,b,carry,result,nzvc,expected,expected[7],expected==0,ev,ec);
        end
        $display("PASS byte ALU: %0d exhaustive low-byte operand/C/result/NZVC checks",checks);$finish;
    end
endmodule
