`timescale 1ns/1ps
module tb_decode;
    reg [15:0] decode_ir=0;
    reg [15:0] psw=0,mmr3=0;
    reg fpp_enabled=0;
    wire [9:0] decoded_legacy;
    reg [11:0] dispatch;
    wire [11:0] actual;
    uj11_decode reference_legacy(decode_ir,decoded_legacy);
    uj11_mmu_decode dut(decode_ir,psw[15:14],mmr3[3],fpp_enabled,actual);
    always @* begin
        dispatch={2'b0,decoded_legacy};
        if(decode_ir>=16'o10 && decode_ir<=16'o77)dispatch=12'h042;
        if(decode_ir==0)dispatch=psw[15:14]==0 ? 12'h500 : 12'h040;
        if(decode_ir==5 && psw[15:14]!=0)dispatch=12'h0b9;
        if((decode_ir & 16'o177770)==16'o230 && psw[15:14]!=0)dispatch=12'h0b9;
        if((decode_ir & 16'o077700)==16'o6500)dispatch=12'h400;
        if((decode_ir & 16'o077700)==16'o6600)dispatch=12'h420;
        if((decode_ir & 16'o177700)==16'o7000 && mmr3[3] && psw[15:14]!=0)dispatch=12'h440;
        if((decode_ir & 16'o177700)==16'o7200 && decode_ir[5:3]!=0)dispatch=12'h470;
        if((decode_ir & 16'o177700)==16'o7300 && decode_ir[5:3]!=0)dispatch=12'h478;
        if(decode_ir[15:12]==15)dispatch=fpp_enabled ? 12'h700 : 12'h042;
    end

    integer i,m,c,f,checks=0;
    initial begin
        for(f=0;f<2;f++)for(c=0;c<2;c++)for(m=0;m<4;m++)for(i=0;i<65536;i++)begin
            decode_ir=i;psw=m<<14;mmr3=c<<3;fpp_enabled=f;#1;
            if(actual!==dispatch)$fatal(1,"opcode=%o mode=%0d csm=%b fpp=%b got=%h expected=%h",decode_ir,m,c,f,actual,dispatch);
            checks++;
        end
        $display("PASS MMU decoder: %0d opcode/mode/FPP/CSM combinations",checks);$finish;
    end
endmodule
