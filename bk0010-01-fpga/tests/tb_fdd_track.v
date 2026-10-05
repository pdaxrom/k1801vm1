`timescale 1ns/1ps
module tb_fdd_track;
    reg clk=0,reset=1,quiesce=0,writing=0,selected=1,side=0;
    always #5 clk=!clk;
    reg [1:0] drive=0;
    reg [6:0] track=0;
    reg [3:0] present=3,readonly=2;
    wire valid,idle,error,request,sd_write;
    wire [1:0] sd_drive;
    wire [10:0] offset;
    wire [8:0] ba;
    wire bw;
    wire [7:0] bd;
    reg [7:0] br=0,buffer[0:511];
    reg [11:0] position=0,wa=0;
    reg ww=0;
    reg [16:0] wd=0;
    wire [16:0] word_data;
    wire [15:0] crc_old,crc_value,next_crc;
    bk_crc16_word crc_datapath(crc_old,crc_value,next_crc);
    reg ready=1,done=0,sd_error=0;
    reg [3:0] delay=0;
    integer reads=0,writes=0,byte_index,word_index;
    reg inject_failure=0;
    bk_fdd_track dut(clk,reset,quiesce,writing,selected,drive,track,side,present,readonly,
        valid,idle,error,position,word_data,ww,wa,wd,request,sd_write,sd_drive,offset,
        ready,done,sd_error,ba,bw,bd,br,next_crc,crc_old,crc_value);
    function [7:0] pattern(input [1:0] d,input [10:0] s,input integer b);
        pattern=d+s*31+b*13;
    endfunction
    always @(posedge clk) begin
        br<=buffer[ba]; if(bw && ready)buffer[ba]<=bd;
        done<=0;sd_error<=0;
        if(ready && request)begin ready<=0;delay<=8;end
        else if(delay!=0)begin
            delay<=delay-1;
            if(delay==1)begin
                if(offset>=1600)$fatal(1,"CHS overflow");
                if(sd_write)begin
                    if(sd_drive!=0 || offset>9)$fatal(1,"Flush escaped cached drive/track");
                    for(byte_index=0;byte_index<512;byte_index=byte_index+1)
                        if(buffer[byte_index] !== (offset==0 ? 8'h42 : pattern(sd_drive,offset,byte_index)))
                            $fatal(1,"Bad flushed sector=%0d byte=%0d got=%02h expected=%02h",offset,byte_index,
                                   buffer[byte_index],offset==0 ? 8'h42 : pattern(sd_drive,offset,byte_index));
                    if(inject_failure)begin sd_error<=1;inject_failure<=0;end
                    else writes<=writes+1;
                end else begin
                    for(byte_index=0;byte_index<512;byte_index=byte_index+1)
                        buffer[byte_index]<=pattern(sd_drive,offset,byte_index);
                    reads<=reads+1;
                end
                done<=1;
            end
        end else if(!request)ready<=1;
    end
    task read_word(input integer address,input [16:0] expected);
        begin
            @(negedge clk);position=address;repeat(2)@(negedge clk);
            if(word_data!==expected)$fatal(1,"Track word %0d got=%05h expected=%05h",address,word_data,expected);
        end
    endtask
    task write_word(input integer address,input [16:0] value);
        begin
            @(negedge clk);wa=address;wd=value;ww=1;
            @(negedge clk);ww=0;
        end
    endtask
    integer sector,base;
    initial begin
        #30;reset=0;wait(valid);
        if(reads!=10 || error)$fatal(1,"Initial track load");
        read_word(0,17'h04e4e);read_word(21,0);
        for(sector=0;sector<10;sector=sector+1)begin
            base=22+sector*311;
            read_word(base,17'h1a1a1);read_word(base+1,17'h0a1fe);
            read_word(base+2,0);read_word(base+3,{5'b0,sector[3:0]+1'b1,8'd2});
            read_word(base+22,17'h1a1a1);read_word(base+23,17'h0a1fb);
            for(word_index=0;word_index<256;word_index=word_index+1)
                read_word(base+24+word_index,{1'b0,pattern(0,sector,word_index*2),pattern(0,sector,word_index*2+1)});
        end
        writing=1;
        for(word_index=0;word_index<256;word_index=word_index+1)write_word(46+word_index,17'h04242);
        // Independent Python binascii.crc_hqx(A1 A1 A1 FB + 512 * 42, FFFF).
        write_word(302,17'h051c8);
        writing=0;quiesce=1;wait(idle);
        if(writes!=10 || error)$fatal(1,"Dirty track was not fully drained");
        // A damaged CRC cannot commit that sector; valid neighbouring sectors still drain.
        @(negedge clk);quiesce=0;writing=1;
        write_word(46,17'h0dead);writing=0;wait(idle);
        if(writes!=19||error)$fatal(1,"Bad-CRC data sector was committed");
        writing=1;write_word(46,17'h04242);writing=0;inject_failure=1;
        wait(error);repeat(20)@(negedge clk);
        if(idle||!dut.dirty||dut.state!=22)$fatal(1,"Failed SD write discarded dirty track");
        quiesce=1;wait(idle);
        if(writes!=29||error)$fatal(1,"RESET drain did not retry preserved track");
        @(negedge clk);quiesce=0;drive=1;track=79;side=1;#1;wait(!valid);wait(valid);#1;
        if(reads!=20 || offset!=1599)$fatal(1,"Upper CHS boundary reads=%0d offset=%0d cache=%0d/%0d/%0d sector=%0d",reads,offset,dut.cached_drive,dut.cached_track,dut.cached_side,dut.sector);
        writing=1;write_word(46,17'h0dead);writing=0;repeat(20)@(negedge clk);
        if(writes!=29 || !idle)$fatal(1,"Read-only track modified");
        $display("PASS track cache: all 10 headers and 2560 payload words, CRC flush/rejection, SD-error retry, CHS 79/1, RO, quiesce");
        $finish;
    end
    initial begin #3000000;$fatal(1,"Track timeout state=%0d sector=%0d index=%0d writes=%0d",dut.state,dut.sector,dut.index,writes);end
endmodule
