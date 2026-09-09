`timescale 1ns/1ps
module tb_decode #(parameter EMIT_CANDIDATES=0);
    reg [15:0] ir;
    wire [9:0] entry;
    reg [9:0] expected;
    integer i,accepted=0,f;
    uj11_decode dut(.*);
    initial begin
        if(EMIT_CANDIDATES)f=$fopen("build/illegal-opcodes.txt","w");
        for(i=0;i<65536;i=i+1) begin
            ir=i[15:0];
            expected=10'h042;
            // Independent octal masks for the exact PDP-11 encodings.
            if((i & 'o177400)>='o000400 && (i & 'o177400)<='o003400)
                expected='h40+((i >> 8)*4);
            if((i & 'o177400)>='o100000 && (i & 'o177400)<='o103400)
                expected='h60+(((i & 'o3400)>>8)*4);
            if((i & 'o170000)=='o010000) expected='h118;
            if((i & 'o170000)=='o020000) expected='h128;
            if((i & 'o170000)=='o060000) expected='h168;
            if((i & 'o170000)=='o030000) expected='h138;
            if((i & 'o170000)=='o040000) expected='h148;
            if((i & 'o170000)=='o050000) expected='h158;
            if((i & 'o170000)=='o160000) expected='h1e8;
            if((i & 'o177070)=='o010000) expected='h110;
            if((i & 'o177070)=='o020000) expected='h120;
            if((i & 'o177070)=='o060000) expected='h160;
            if((i & 'o177070)=='o030000) expected='h130;
            if((i & 'o177070)=='o040000) expected='h140;
            if((i & 'o177070)=='o050000) expected='h150;
            if((i & 'o177070)=='o160000) expected='h1e0;
            if((i & 'o177000)=='o005000 || (i & 'o177400)=='o006000)
                expected=(((i & 'o7700)>>6)*4) + ((i & 'o70)!=0 ? 2 : 0);
            if((i & 'o170000)>='o110000 && (i & 'o170000)<='o150000)
                expected='h100+(((i & 'o70000)>>12)*16)+((i & 'o7070)==0?0:8);
            if((i & 'o177000)=='o105000 || (i & 'o177400)=='o106000)
                expected=(((i & 'o7700)>>6)*4)+((i & 'o70)!=0?2:0);
            if((i & 'o177700)=='o000100 && (i & 'o70)!=0)expected='h080;
            if((i & 'o177000)=='o004000 && (i & 'o70)!=0)expected='h088;
            if((i & 'o177770)=='o000200)expected='h090;
            if((i & 'o177000)=='o077000)expected='h098;
            if((i & 'o177700)=='o000300)expected=(i & 'o70)?'h1c8:'h1c0;
            if((i & 'o177700)=='o006700)expected=(i & 'o70)?'h1b4:'h1b0;
            if((i & 'o177700)=='o006400)expected='h1a0;
            if(i=='o000003)expected='h024;
            if(i=='o000004)expected='h026;
            if(i=='o000002 || i=='o000006)expected='h031;
            if((i & 'o177400)=='o104000)expected='h028;
            if((i & 'o177400)=='o104400)expected='h02a;
            if((i & 'o177700)=='o106700)expected=(i & 'o70)?'h1da:'h1d8;
            if((i & 'o177700)=='o106400)expected=(i & 'o70)?'h1aa:'h1a8;
            if((i & 'o177000)=='o072000)expected='h019;
            if((i & 'o177000)=='o073000)expected='h01c;
            if((i & 'o177000)=='o070000)expected=(i & 'o70)?'h066:'h062;
            if((i & 'o177000)=='o071000)expected=(i & 'o70)?'h076:'h072;
            if((i & 'o177000)=='o074000)expected=(i & 'o70)?'h02e:'h02c;
            if((i & 'o177740)=='o075000)expected='h011;
            if(i=='o000000)expected='h018;
            if(i=='o000005)expected='h021;
            if(i=='o000007)expected='h017;
            if((i & 'o177740)=='o000240)expected='h104+((i & 15)*8);
            if(i=='o000001)expected='h012;
            if((i & 'o177770)=='o000230)expected='h1f0+((i & 7)*2);
            if((i & 'o177770)=='o000100)expected='h040;
            #1;
            if(entry!==expected) $fatal(1,"decoder opcode%06o got%h expected%h",i,entry,expected);
            if(entry!=10'h042 && entry!=10'h040) accepted=accepted+1;
            else if(EMIT_CANDIDATES)$fwrite(f,"%04x %x\n",ir,entry==10'h040 ? 4 : 8);
        end
        if(accepted!=59024) $fatal(1,"accepted opcode count %0d",accepted);
        $display("PASS decoder: all 65536 opcodes, exactly 59024 supported encodings");
        if(EMIT_CANDIDATES)$fclose(f);
        $finish;
    end
endmodule
