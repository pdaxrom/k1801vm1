`timescale 1ns/1ps
module tb_terak_display_ram;
`ifdef UJ11_VIDEO_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,power_on=1;
    always #10 clk=~clk;
    reg cpu_request=0,cpu_write=0,cpu_font=0,video_request=0,video_font=0;
    reg [11:0] cpu_address=0,video_address=0;
    reg [15:0] cpu_data=0;
    wire initialized,cpu_ready,video_ready;
    wire [15:0] cpu_read_data,video_data;
    uj11_terak_display_ram dut(.*);
    function automatic [7:0] pattern(input integer index,input bit font);
        pattern=(index*73)^(index>>3)^(font ? 8'ha6 : 8'h59);
    endfunction
    task cpu_cycle(input bit writing,font,input [11:0] address,input [7:0] value);
        @(negedge clk);cpu_request=1;cpu_write=writing;cpu_font=font;cpu_address=address;
        cpu_data=address[0] ? {value,8'hc3} : {8'h3c,value};
        do @(negedge clk);while(!cpu_ready);
        if(!writing && cpu_read_data!=={value,value})
            $fatal(1,"CPU byte font=%0d address=%o got=%h expected=%h",font,address,cpu_read_data,value);
        // A held request must remain acknowledged without issuing another read.
        repeat(2)@(negedge clk);
        if(!cpu_ready)$fatal(1,"CPU acknowledgement was not held");
        cpu_request=0;@(negedge clk);
    endtask
    task video_cycle(input bit font,input [11:0] address,input [15:0] expected);
        @(negedge clk);video_request=1;video_font=font;video_address=address;
        do @(negedge clk);while(!video_ready);
        if(video_data!==expected)$fatal(1,"video word font=%0d address=%o got=%h expected=%h",
            font,address,video_data,expected);
        repeat(2)@(negedge clk);
        if(!video_ready || video_data!==expected)$fatal(1,"video word/acknowledgement changed while held");
        video_request=0;@(negedge clk);
    endtask
    task check_page;
        for(integer row=0;row<25;row++)for(integer col=0;col<80;col++)
            cpu_cycle(0,0,row*128+col,pattern(row*80+col,0));
    endtask
    task check_font;
        integer index;
        index=0;
        for(integer code=32;code<256;code++)if(code<128 || code>=160)
            for(integer scan=0;scan<10;scan++)begin
                cpu_cycle(0,1,code*16+scan,pattern(index,1));index++;
            end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        for(integer row=0;row<25;row++)for(integer col=0;col<80;col++)
            cpu_cycle(1,0,row*128+col,pattern(row*80+col,0));
        begin: fill_font
            integer index;
            index=0;
            for(integer code=32;code<256;code++)if(code<128 || code>=160)
                for(integer scan=0;scan<10;scan++)begin
                    cpu_cycle(1,1,code*16+scan,pattern(index,1));index++;
                end
        end
        // Sparse native address holes must not alias any live page/glyph byte.
        for(integer address=0;address<4096;address++)begin
            if(address/128>=25 || address%128>=80)begin
                cpu_cycle(1,0,address,8'hff);cpu_cycle(0,0,address,0);
            end
            if((address/16)%128<32 || address%16>=10)begin
                cpu_cycle(1,1,address,8'hff);cpu_cycle(0,1,address,0);
            end
        end
        check_page();check_font();
        // Exercise both ports together, all page rows (including hidden row 25),
        // every glyph and scan pair, and both 1024-byte EBR boundaries.
        fork
            begin check_page();check_font();end
            begin
                for(integer row=0;row<25;row++)for(integer col=0;col<80;col+=2)
                    video_cycle(0,row*128+col,{pattern(row*80+col+1,0),pattern(row*80+col,0)});
                begin: video_font_check
                    integer index;
                    index=0;
                    for(integer code=32;code<256;code++)if(code<128 || code>=160)
                        for(integer scan=0;scan<10;scan+=2)begin
                            video_cycle(1,code*16+scan,{pattern(index+1,1),pattern(index,1)});index+=2;
                        end
                end
            end
        join
        @(negedge clk);reset=1;repeat(5)@(negedge clk);reset=0;
        check_page();check_font(); // Peripheral/CPU reset preserves the RAM.
        @(negedge clk);power_on=1;repeat(5)@(negedge clk);power_on=0;wait(initialized);
        cpu_cycle(0,0,0,0);cpu_cycle(0,0,24*128+79,0);
        cpu_cycle(0,1,32*16,0);cpu_cycle(0,1,255*16+9,0);
        $display("PASS separate TERAK page/font RAM: all 3920 bytes, sparse holes, both byte lanes, simultaneous ports, reset preservation and cold clear");
        $finish;
    end
    initial begin #10000000;$fatal(1,"display RAM timeout");end
endmodule
