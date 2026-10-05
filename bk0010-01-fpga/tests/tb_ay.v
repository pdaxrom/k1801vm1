`timescale 1ns/1ps
module tb_ay;
    reg clk=0,reset=0,tick=0,write=0;
    reg [3:0] address=0;
    reg [7:0] data=0;
    wire [8:0] pcm;
    bk_ay dut(clk,reset,tick,write,address,data,pcm);
    integer file,code,expected,cycle=0;
    initial begin
        file=$fopen("build/ay-vectors.txt","r");
        if(!file) $fatal(1,"Missing AY oracle vectors");
        while(!$feof(file)) begin
            code=$fscanf(file,"%d %d %d %d %d %d\n",reset,tick,write,address,data,expected);
            if(code!=6) $fatal(1,"Malformed AY vector");
            #5; clk=1; #1;
            if(pcm !== expected) $fatal(1,"AY PCM cycle %0d: %0d expected %0d",cycle,pcm,expected);
            #4; clk=0; cycle=cycle+1;
        end
        $fclose(file);
        $display("PASS AY independent PCM oracle: %0d cycles, tones/noise/mixer/all envelopes",cycle);
        $finish;
    end
endmodule
