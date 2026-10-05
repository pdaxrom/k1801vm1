`timescale 1ns/1ps
module tb_pal_encoder;
    reg clk=0,reset=1,sync=0,burst=0,active=0,alternate=0;
    reg [7:0] rgb=0;
    wire [5:0] actual,expected;
    always #7.8125 clk=!clk;
`include "rtl/bk_rgb_index.vh"
`include "rtl/bk_palette_index.vh"
`include "rtl/bk_palettes.vh"
    bk_pal_encoder compact(clk,reset,sync,burst,active,alternate,bk_rgb_index(rgb),actual);
    uj11_pal_encoder original(clk,reset,sync,burst,active,alternate,rgb,expected);
    reg [7:0] colors[0:13];
    integer index,control,cycles=0;
    always @(negedge clk)begin
        if(!reset&&cycles>5&&actual!==expected)
            $fatal(1,"PAL sample differs: actual=%0d expected=%0d phase=%08h",actual,expected,compact.phase);
        cycles=cycles+1;
    end
    initial begin
        colors[0]=0;colors[1]=3;colors[2]=8'h1c;colors[3]=8'h1f;
        colors[4]=8'h5c;colors[5]=8'h60;colors[6]=8'h63;colors[7]=8'ha0;
        colors[8]=8'ha3;colors[9]=8'hbc;colors[10]=8'he0;colors[11]=8'he3;
        colors[12]=8'hfc;colors[13]=8'hff;
        repeat(8)@(negedge clk);reset=0;
        for(control=0;control<64;control=control+1)
            if(bk_palette_index(control>>2,control&3)!==bk_rgb_index(bk_palette_rgb(control>>2,control&3)))
                $fatal(1,"Palette index mismatch %0d",control);
        for(control=0;control<16;control=control+1)begin
            for(index=0;index<14;index=index+1)begin
                {sync,burst,active,alternate}=control;rgb=colors[index];
                repeat(1200)@(negedge clk);
            end
        end
        $display("PASS PAL table: all 14 BK RGB332 colours, phase/alternation, all active/burst/sync combinations, identical 5-clock DAC pipeline");
        $finish;
    end
endmodule
