`timescale 1ns/1ps
// Real SERV and SD card, with an independent bus master checking QB DMA and mixed-density media.
module tb_terak_qb #(parameter DISPLAY_EBR=0,RAM_BYTES=57344);
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0,request=0,writing=0,byte_access=0,irq_ack=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg [21:0] address=0;
    reg [15:0] wd=0;
    wire ready,error,initialized,cpu_start,irq_valid;
    wire [15:0] rd,vector;
    wire [2:0] priority_level;
    wire [19:0] sa;
    wire [15:0] sd;
    wire ce,oe,we,lb,ub,cs,sck,mosi,miso;
    tri1 ps2_clock;
    uj11_terak_bus #(.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.CLEAR_WORDS(1)) bus(.clk(clk),.reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.video_clk(vclk),.video_reset(1'b0),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(1'b0),.address(address),.write_data(wd),
        .ready(ready),.error(error),.read_data(rd),.irq_valid(irq_valid),.irq_priority(priority_level),.irq_vector(vector),.irq_ack(irq_ack),
        .ps2_clock(ps2_clock),.ps2_data(1'b1),.tvout(),.audio(),.memory_initialized(initialized),.cpu_start(cpu_start),
        .boot_complete(),.sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model #(.SECTORS(256)) card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),
        .absent(1'b0),.fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    integer data_base=0,data_blocks=0,last_writes=0;
    initial begin
        if($value$plusargs("DATA_BASE=%d",data_base))begin
            if(!$value$plusargs("DATA_BLOCKS=%d",data_blocks))$fatal(1,"disk extent required");
        end
    end
    always @(negedge clk)if(data_base && card.write_commands!=last_writes)begin
        if(card.write_lba<data_base || card.write_lba>=data_base+data_blocks)
            $fatal(1,"disk write escaped partition into MBR/FAT/other media: %0d",card.write_lba);
        last_writes=card.write_commands;
    end
    serv_memory_guard guard(.clk(clk),.reset(bus.disk.iop_reset),
        .write(bus.disk.data_accept && bus.disk.memory_selected && bus.disk.de && bus.disk.da<(DISPLAY_EBR ? 14848 : 18944)),.address(bus.disk.da));
    reg [15:0] result;
    integer readonly_test;
    task access(input [21:0] a,input bit w,input [15:0] data);
        @(negedge clk);address=a;writing=w;wd=data;request=1;
        do @(negedge clk);while(!ready);
        if(error)$fatal(1,"unexpected bus error at %o",a);
        result=rd;request=0;repeat(2)@(negedge clk);
    endtask
    reg [7:0] status[0:6];
    task send_byte(input [7:0] b);
        do access(22'o17771000,0,0);while(!result[7]);
        if(result[6])$fatal(1,"QB expected command direction %h",result);
        access(22'o17771002,1,{8'b0,b});
    endtask
    task receive(input integer count);
        for(integer i=0;i<count;i++)begin
            do access(22'o17771000,0,0);while(!result[7]);
            if(!result[6])$fatal(1,"QB expected result direction %h",result);
            access(22'o17771002,0,0);status[i]=result[7:0];
        end
    endtask
    task seek(input integer c);
        send_byte(15);send_byte(0);send_byte(8'(c));
        do access(22'o17771000,0,0);while(!result[7]);
        send_byte(8);receive(2);
        if(status[0]!=32 || status[1]!=c)$fatal(1,"QB seek/SIS %h %h",status[0],status[1]);
    endtask
    task program_dma(input integer a,input integer count,input bit read);
        access(22'o17771016,1,16'(a));access(22'o17771016,1,16'(a>>8));
        access(22'o17771014,1,16'(count-1));
        access(22'o17771014,1,16'(((count-1)>>8)|(read ? 8'h40 : 8'h80)));
        access(22'o17771012,1,read ? 16'h51 : 16'h41);
    endtask
    task transfer(input [7:0] op,input integer c,input integer h,input integer r,input integer n,
                  input integer eot,input integer count,input integer a);
        program_dma(a,count,op[4:0]==6 || op[4:0]==12 || op[4:0]==2);
        send_byte(op);send_byte(8'(h<<2));send_byte(8'(c));send_byte(8'(h));send_byte(8'(r));
        send_byte(8'(n));send_byte(8'(eot));send_byte(27);send_byte(255);receive(7);
    endtask
    task clean_status;
        if(status[0][7:6] || status[1] || status[2])
            $fatal(1,"QB unexpected ST0/1/2 %h %h %h",status[0],status[1],status[2]);
    endtask
    task check_pattern(input integer c,input integer h,input integer r,input integer bytes,input integer a);
        for(integer i=0;i<bytes/2;i++)begin
            access(22'(a+2*i),0,0);
            if(result!={8'(c*11+h*37+r*17+2*i+1),8'(c*11+h*37+r*17+2*i)})
                $fatal(1,"QB payload c/h/r=%d/%d/%d i=%d got=%h",c,h,r,i,result);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;wait(cpu_start);
        if(bus.qx.present!=1)$fatal(1,"QB common partition mount failed");
        send_byte(3);send_byte(8'haf);send_byte(36);
        // Disabled IRQ is retained and becomes visible when enabled, at the
        // original QB.SYS vector. A pending BEVNT is acknowledged first.
        access(22'o17771012,1,8);
        send_byte(15);send_byte(0);send_byte(1);
        do access(22'o17771000,0,0);while(!result[7]);
        if(bus.qb_irq)$fatal(1,"QB IRQ disable ignored");
        access(22'o17771012,1,0);
        wait(bus.qb_irq);
        while(priority_level==6)begin
            @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
        end
        if(!irq_valid || vector!=16'o234)$fatal(1,"QB native vector %o",vector);
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
        wait(!bus.qb_irq);
        send_byte(8);receive(2);
        if(status[0]!=32 || status[1]!=1)$fatal(1,"QB acknowledged seek result lost");
        seek(0);
        transfer(6,0,0,1,0,26,128,16'h2000);clean_status();check_pattern(0,0,1,128,16'h2000);
        transfer(8'h46,0,1,1,1,26,256,16'h2200);clean_status();check_pattern(0,1,1,256,16'h2200);
        seek(1);
        if(RAM_BYTES>57344)begin
            // Native QB DMA still has only 16 address bits on an 11/23.
            transfer(8'h46,1,0,3,2,15,512,16'he000);clean_status();
            check_pattern(1,0,3,512,16'he000);
            access(22'h10000,1,16'h1234);access(22'hfefe,1,16'hbeef);
            transfer(8'h46,1,0,3,2,15,512,16'hff00);clean_status();
            for(integer i=0;i<256;i++)begin
                access(22'((16'hff00+2*i)&65535),0,0);
                if(result!={8'(11+3*17+2*i+1),8'(11+3*17+2*i)})
                    $fatal(1,"16-bit QBBA wrap at word %0d: %h",i,result);
            end
            access(22'h10000,0,0);if(result!=16'h1234)$fatal(1,"QB DMA spilled into bank 8");
            access(22'hfefe,0,0);if(result!=16'hbeef)$fatal(1,"QB DMA changed preceding word");
            access(22'hfffe,1,16'hccaa);access(0,1,16'hbbbb);access(2,1,16'hcccc);
            transfer(8'h46,1,0,3,2,15,3,16'hffff);clean_status();
            access(22'hfffe,0,0);if(result!=16'h3eaa)$fatal(1,"odd DMA wrap first byte %h",result);
            access(0,0,0);if(result!=16'h403f)$fatal(1,"odd DMA wrap tail %h",result);
            access(2,0,0);if(result!=16'hcccc)$fatal(1,"odd DMA wrap changed neighbor");
            access(22'h10000,0,0);if(result!=16'h1234)$fatal(1,"odd DMA escaped 16-bit QBBA");
            $display("PASS expanded RAM QB DMA upper low-64K buffer, 16-bit wrap and odd-byte neighbors");
        end
        transfer(8'h46,1,0,1,2,15,512,16'h2400);clean_status();check_pattern(1,0,1,512,16'h2400);
        transfer(8'h46,1,1,1,2,15,512,16'h2600);clean_status();check_pattern(1,1,1,512,16'h2600);
        // A 3-byte transfer at an odd address must preserve both neighboring bytes.
        access(22'h2800,1,16'haaaa);access(22'h2802,1,16'hbbbb);access(22'h2804,1,16'hcccc);
        transfer(8'h46,1,0,2,2,15,3,16'h2801);clean_status();
        access(22'h2800,0,0);if(result!=16'h2daa)$fatal(1,"odd first byte %h",result);
        access(22'h2802,0,0);if(result!=16'h2f2e)$fatal(1,"odd tail %h",result);
        access(22'h2804,0,0);if(result!=16'hcccc)$fatal(1,"odd neighbor changed");
        // Deleted/CRC/absent records must survive IMD import and reach ST1/ST2.
        if(!$test$plusargs("RAW"))begin
        transfer(8'h46,1,0,5,2,15,512,16'h2400);
        if(status[2]!=64 || status[0]!=64)$fatal(1,"deleted read status");
        transfer(8'h4c,1,0,5,2,15,512,16'h2400);clean_status();
        transfer(8'h46,1,0,6,2,15,512,16'h2400);
        if(status[1]!=32 || status[2]!=32)$fatal(1,"CRC status");
        transfer(8'h46,1,0,7,2,15,512,16'h2400);
        if(status[1]!=1 || status[2]!=1)$fatal(1,"missing address mark");
        end
        transfer(6,1,0,1,2,15,512,16'h2400);
        if(status[1]!=5)$fatal(1,"density mismatch");
        if($test$plusargs("READONLY"))begin
            transfer(8'h45,1,0,1,2,15,512,16'h2400);
            if(status[1]!=2 || card.write_commands)$fatal(1,"QB readonly modified SD");
            if($test$plusargs("RAW"))$display("PASS raw QB readonly, FM/MFM sides, variable sector sizes and odd DMA");
            else $display("PASS QB readonly, FM/MFM sides, variable sector sizes, odd DMA and IMD errors");
        end else begin
            // Partial write preserves the rest of this sector and the next sector.
            access(22'h2800,1,16'haaaa);access(22'h2802,1,16'hbbbb);
            transfer(8'h45,1,0,2,2,15,3,16'h2801);clean_status();
            transfer(8'h46,1,0,2,2,15,512,16'h2400);clean_status();
            access(22'h2400,0,0);if(result!=16'hbbaa)$fatal(1,"QB partial write start");
            access(22'h2402,0,0);if(result!=16'h30bb)$fatal(1,"QB partial write tail");
            transfer(8'h46,1,0,3,2,15,512,16'h2600);clean_status();check_pattern(1,0,3,512,16'h2600);
            if($test$plusargs("RAW"))begin
                integer before_writes;
                before_writes=card.write_commands;
                transfer(8'h49,1,1,2,2,15,512,16'h2600);
                if(status[1]!=4 || card.write_commands!=before_writes)$fatal(1,"raw deleted write changed SD");
            end else begin
            transfer(8'h49,1,1,2,2,15,512,16'h2600);clean_status();
            transfer(8'h4c,1,1,2,2,15,512,16'h2400);clean_status();check_pattern(1,0,3,512,16'h2400);
            end
            // MT crosses side 0 sector 15 to side 1 sector 1 without stepping.
            transfer(8'hc6,1,0,15,2,15,1024,16'h3000);clean_status();
            check_pattern(1,0,15,512,16'h3000);check_pattern(1,1,1,512,16'h3200);
            if($test$plusargs("RAW"))begin
                integer before_writes;
                seek(2);
                access(22'h2000,1,16'h0002);access(22'h2002,1,16'h0201);
                access(22'h2004,1,16'h0002);access(22'h2006,1,16'h0202);
                before_writes=card.write_commands;
                program_dma(16'h2000,8,0);
                send_byte(8'h4d);send_byte(0);send_byte(2);send_byte(2);send_byte(84);send_byte(8'he5);
                receive(7);
                if(status[1]!=4 || card.write_commands!=before_writes)$fatal(1,"raw geometry change wrote SD");
                // Native 15x512 format must preserve the shared SD block at
                // each edge of this unaligned track, including adjacent head.
                for(integer r=1;r<=15;r++)begin
                    access(22'(16'h2000+4*(r-1)),1,16'h0002);
                    access(22'(16'h2002+4*(r-1)),1,16'h0200+r);
                end
                program_dma(16'h2000,60,0);
                send_byte(8'h4d);send_byte(0);send_byte(2);send_byte(15);send_byte(84);send_byte(8'he5);
                receive(7);clean_status();
                transfer(8'h46,2,0,1,2,15,512,16'h3000);clean_status();
                for(integer i=0;i<256;i++)begin access(22'(16'h3000+i*2),0,0);if(result!=16'he5e5)$fatal(1,"raw format payload");end
                transfer(8'h46,2,1,1,2,15,512,16'h3000);clean_status();check_pattern(2,1,1,512,16'h3000);
                transfer(8'h46,2,1,15,2,15,512,16'h3000);clean_status();check_pattern(2,1,15,512,16'h3000);
                seek(1);
                transfer(8'h46,1,1,15,2,15,512,16'h3000);clean_status();check_pattern(1,1,15,512,16'h3000);
                $display("PASS raw QB format/RMW preserves unaligned neighboring tracks");
            end else begin
            // Format a new 2-sector MFM track from DMA ID quadruples.
            seek(2);
            access(22'h2000,1,16'h0002);access(22'h2002,1,16'h0201);
            access(22'h2004,1,16'h0002);access(22'h2006,1,16'h0202);
            program_dma(16'h2000,8,0);
            send_byte(8'h4d);send_byte(0);send_byte(2);send_byte(2);send_byte(84);send_byte(8'he5);
            receive(7);clean_status();
            transfer(8'h46,2,0,1,2,2,1024,16'h3000);clean_status();
            for(integer i=0;i<512;i++)begin access(22'(16'h3000+i*2),0,0);if(result!=16'he5e5)$fatal(1,"QB formatted payload");end
            end
            if($test$plusargs("RAW"))$display("PASS raw QB FM/MFM sides, DMA/odd/MT/RMW, seek and fixed-geometry format");
            else $display("PASS QB FM/MFM sides, DMA/odd/MT/RMW, deleted/CRC/absent, seek and format");
        end
        // Reset must clear IRQ/command state and DMA programming, preserve mounts.
        peripheral_reset=1;repeat(3)@(negedge clk);peripheral_reset=0;
        do access(22'o17771000,0,0);while(!result[7]);
        if(result!=16'h8080 || bus.qx.present!=1)$fatal(1,"QB RESET state");
        send_byte(4);send_byte(0);receive(1);
        if((status[0]&8'h38)!=8'h38)$fatal(1,"QB sense drive status %h",status[0]);
        send_byte(4);send_byte(3);receive(1);
        if(status[0]&32)$fatal(1,"QB absent drive ready");
        // Abort an actual word DMA, then issue a fresh command. Its result
        // must not be overwritten by the operation from before RESET.
        seek(1);program_dma(16'h3000,512,1);
        send_byte(8'h46);send_byte(0);send_byte(1);send_byte(0);send_byte(1);
        send_byte(2);send_byte(15);send_byte(27);send_byte(255);
        wait(bus.dma_request);
        @(negedge clk);peripheral_reset=1;repeat(3)@(negedge clk);peripheral_reset=0;
        do access(22'o17771000,0,0);while(!result[7] || result[6]);
        send_byte(4);send_byte(0);receive(1);
        if((status[0]&8'h38)!=8'h38)$fatal(1,"QB stale RESET completion");
        send_byte(8'h0a);send_byte(0);receive(7);clean_status();
        if(status[3]!=0 || status[4]!=0 || status[5]!=1 || status[6]!=0)$fatal(1,"QB first READ ID");
        send_byte(8'h0a);send_byte(0);receive(7);clean_status();
        if(status[5]!=2)$fatal(1,"QB READ ID did not rotate");
        $display("PASS QB RESET and drive status");$finish;
    end
    initial begin #2000000000;$fatal(1,"QB integration timeout");end
endmodule
