`timescale 1ns/1ps
module tb_fp_datapath;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,request=0,writing=0;
    reg [4:0] address=0;reg [15:0] write_data=0;
    wire [15:0] read_data;wire ready;
    uj11_mmu_fp_datapath dut(.*);
    reg [63:0] a,b,result;
    reg [127:0] golden;
    reg [15:0] status;
    integer checks=0,n;
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s A=%h B=%h result=%h",why,a,b,result);checks++;end
    endtask
    task put(input [4:0] addr,input [15:0] value);
        begin
            @(negedge clk);request=1;writing=1;address=addr;write_data=value;n=0;
            do begin @(posedge clk);n++;check(n<10,"bounded micro-operation");end while(!ready);
            @(negedge clk);request=0;@(negedge clk);
        end
    endtask
    task command(input [4:0] op);put(16,{11'b0,op});endtask
    task wide(input [4:0] base,input [63:0] value);
        begin for(integer i=0;i<4;i++)put(base+5'(i),value[63-16*i-:16]);end
    endtask
    task read_x;
        begin
            for(integer i=0;i<4;i++)begin address=5'(i);#1;result[63-16*i-:16]=read_data;end
        end
    endtask
    task read_status;
        begin address=16;#1;status=read_data;end
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        for(integer t=0;t<256;t++)begin
            a={$random,$random};b={$random,$random};
            wide(0,a);wide(4,b);read_status();
            check(status[3:0]=={a==b,a<b,b==0,a==0},"unsigned status");
            command(13);read_x();check(result==a+b,"serial 64-bit ADD");
            command(14);read_x();check(result==a,"serial 64-bit SUB");
            command(23);read_x();check(result==-a,"serial 64-bit NEGATE");
            wide(0,a);command(27);read_x();check(result==(a&b),"serial AND");
            wide(0,a);command(28);read_x();check(result==(a&~b),"serial BIC");
            wide(0,a);command(10);read_x();check(result==(a<<1),"left shift");
            wide(0,a);command(11);read_x();check(result==(a>>1),"right shift");
            wide(4,b);command(12);command(5);read_x();check(result==((b>>1)|{63'b0,b[0]}),"sticky alignment");
            for(integer d=0;d<2;d++)begin
                put(20,16'(d));wide(0,a);command(15);read_x();
                check(result==a+(d ? 64'h40 : 64'h4000000000),"precision rounding increment");
                wide(0,a);command(7);command(9);read_x();
                check(result==(a[62:55]==0 ? 64'b0 : d ? a : (a&64'hffffffff00000000)),"pack/unpack including dirty zero");
            end
            // The same single-step operations the microcode uses. Golden
            // products/division use independent Verilog 128-bit arithmetic.
            a={2'b01,a[61:0]};b={2'b01,b[61:0]};
            wide(0,a);wide(4,b);command(19);
            for(integer i=0;i<64;i++)begin
                read_status();if(status[8])command(16);command(17);
            end
            command(18);read_x();golden={64'b0,a}*{64'b0,b};
            check(result==golden[126:63],"serial multiplication loop");
            if(a<b)a=a<<1;
            wide(0,a);wide(4,b);command(3);
            for(integer i=0;i<63;i++)begin
                command(20);read_status();if(!status[2])begin command(14);command(21);end
                command(10);
            end
            command(22);read_x();golden=({64'b0,a}<<62)/{64'b0,b};
            check(result==golden[63:0],"restoring division loop");
        end
        $display("PASS MMU FP datapath: %0d checks",checks);$finish;
    end
endmodule
