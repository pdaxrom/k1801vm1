`timescale 1ns/1ps
module tb_terak_video;
`ifdef UJ11_VIDEO_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,vclk=0,reset=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg [15:0] gar=16'o20000,vir=0,vcr=16'o77;
    wire req;
    wire [19:0] address;
    reg ready=0;
    reg [15:0] data=0;
    reg [15:0] mem[0:65535];
    reg seen=0;
    wire [5:0] dac;
    wire [15:0] underruns;
    uj11_terak_video video(.clk(clk),.video_clk(vclk),.reset(reset),.gar(gar),.vir(vir),.vcr(vcr),
        .memory_request(req),.memory_address(address),.memory_ready(ready),.memory_data(data),.dac(dac),.underruns(underruns),.field_toggle());
    always @(posedge clk)begin
        if(reset || !req)begin ready<=0;seen<=0;end
        else if(!seen)begin data<=mem[address];ready<=1;seen<=1;end
    end
    integer lines=0,white_pixels=0,gray_pixels=0;
    always @(posedge vclk)if(!reset)begin
        if(dac==50)white_pixels++;
        if(dac==32)gray_pixels++;
    end
    integer output_column=0,pulse_left=0,checked_columns=0;
    reg [15:0] expected_chars,expected_font,expected_graph;
    reg [7:0] expected_code;
    reg [5:0] expected_dac;
    // Compare the serialized DAC against guest memory, including the first
    // and last columns and all twenty short pixels in each field's first row.
    always @(negedge vclk)if(!reset && (video.line_number==18 || video.line_number==281))begin
        if(video.x==696)begin output_column=0;pulse_left=5;end
        if(video.x>=696 && video.x<=3875)begin
            if(output_column>=640)$fatal(1,"too many serialized columns");
            expected_chars=mem[20'h08000+(output_column>>4)];
            expected_code=output_column[3] ? expected_chars[15:8] : expected_chars[7:0];
            expected_font=mem[20'h08800+expected_code*8];
            expected_graph=mem[16'o20000/2+(output_column>>5)];
            expected_dac=expected_code>=32 && expected_font[output_column&7] ? 50 :
                expected_graph[(output_column>>1)&15] ? 32 : 15;
            if(dac!==expected_dac)$fatal(1,"serialized column %0d x=%0d DAC %0d/%0d",
                output_column,video.x,dac,expected_dac);
            pulse_left--;
            if(pulse_left==0)begin
                output_column++;checked_columns++;
                pulse_left=(output_column&31)==15 ? 4 : 5;
            end
        end
        if(video.x==3876 && (output_column!=640 || dac!==15))$fatal(1,"last column/blanking boundary");
    end
    always @(posedge clk)if(!reset && video.state==6)begin
        if(video.fill_row==0)begin
`ifndef UJ11_VIDEO_VENDOR_RAM
            // Four glyphs, bit zero at the left. Graphics bit zero is doubled.
            if(video.line_ram.bytes[0]!==8'h12 || video.line_ram.bytes[1]!==0)
                $fatal(1,"native first glyph/graphics order %h %h",video.line_ram.bytes[0],video.line_ram.bytes[1]);
            if(video.line_ram.bytes[4]!==8'h20)$fatal(1,"second glyph/page stride");
            if(video.line_ram.bytes[16]!==8'h22)$fatal(1,"next graphics word/page stride");
`endif
        end
        lines++;
    end
    initial begin
        for(integer i=0;i<65536;i++)mem[i]=0;
        mem[16'o20000/2]=1;
        for(integer row=0;row<25;row++)begin
            mem[20'h08000+row*64]=16'h4241;
            mem[20'h08000+row*64+1]=16'h4141;
            mem[20'h08000+row*64+2]=16'h4343;
            for(integer group=2;group<20;group++)begin
                mem[20'h08000+row*64+group*2]=16'h4241;
                mem[20'h08000+row*64+group*2+1]=16'h4343;
            end
        end
        for(integer group=2;group<20;group++)mem[16'o20000/2+group]=16'ha55a^(group*16'h1357);
        mem[16'o20000/2+19]|=16'h8000;
        for(integer scan=0;scan<10;scan++)begin
            mem[20'h08800+65*8+(scan>>1)]|=16'h0001<<(8*(scan&1));
            mem[20'h08800+66*8+(scan>>1)]|=16'h0002<<(8*(scan&1));
            mem[20'h08800+67*8+(scan>>1)]|=16'h0003<<(8*(scan&1));
        end
        repeat(5)@(negedge clk);reset=0;
        wait(lines>=480);if(white_pixels==0 || gray_pixels==0)$fatal(1,"missing native DAC pixels white=%0d gray=%0d",white_pixels,gray_pixels);
        if(underruns)$fatal(1,"line deadline underruns %d",underruns);
        if(checked_columns!=1280)$fatal(1,"missing 640-column field checks %0d",checked_columns);
        $display("PASS native Terak graphics, glyphs, text stride, all 640 serialized columns, short pixels and 480 line deadlines");$finish;
    end
    initial begin #40000000;$fatal(1,"video timeout");end
endmodule
