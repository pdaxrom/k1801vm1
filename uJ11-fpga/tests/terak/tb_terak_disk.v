`timescale 1ns/1ps
// Real SERV and SD card, with an independent bus master checking 128-byte RMW.
module tb_terak_disk #(parameter DISPLAY_EBR=0,RAM_BYTES=57344);
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
        .uart_rx(1'b1),.uart_tx(),.ps2_clock(ps2_clock),.ps2_data(1'b1),.tvout(),.audio(),.memory_initialized(initialized),.cpu_start(cpu_start),
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
    task complete;
        do access(22'o17777000,0,0);while(!result[7]);
        if(result[15])$fatal(1,"QX error %o",result);
    endtask
    task sector_id(input integer number);
        do begin
            access(22'o17777000,1,16'o31);complete();access(22'o17777002,0,0);
        end while(result[15:8]!=number);
    endtask
    task read_sector(input integer number);
        sector_id(number);access(22'o17777000,1,16'o33);complete();
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;wait(cpu_start);
        if(bus.qx.present!=1)$fatal(1,"label mount failed");
        for(integer s=1;s<=4;s++)begin
            read_sector(s);
            for(integer i=0;i<64;i++)begin
                access(22'o17777002,0,0);
                if(result!={8'(s*17+2*i+1),8'(s*17+2*i)})$fatal(1,"fixture read s=%0d i=%0d=%h",s,i,result);
            end
        end
        readonly_test=$test$plusargs("READONLY");
        if(readonly_test)begin
            if(bus.qx.readonly!=1)$fatal(1,"readonly label lost");
            sector_id(2);access(22'o17777000,1,16'o35);
            do access(22'o17777000,0,0);while(!result[7]);
            if((result&16'hfc00)!=16'h8800 || card.write_commands)$fatal(1,"write protection");
            $display("PASS readonly QX card rejects writes without SD mutation");$finish;
        end
        if(!$test$plusargs("RAW"))for(integer s=5;s<=7;s++)begin
            sector_id(s);access(22'o17777000,1,16'o33);
            do access(22'o17777000,0,0);while(!result[7]);
            if((result&16'hfc00)!=(s==5 ? 16'h8400 : s==6 ? 16'h9000 : 16'hc000))
                $fatal(1,"IMD deleted/CRC/absent status s=%d CSR=%o",s,result);
            if(s!=7)begin access(22'o17777002,0,0);if(result!=16'h5a5a)$fatal(1,"flagged payload");end
        end
        access(22'o17777000,0,0);
        for(integer i=0;i<64;i++)access(22'o17777002,1,16'ha000+i);
        sector_id(2);access(22'o17777000,1,16'o35);complete();
        for(integer s=1;s<=4;s++)begin
            read_sector(s);
            for(integer i=0;i<64;i++)begin
                access(22'o17777002,0,0);
                if(result!=(s==2 ? 16'ha000+i : {8'(s*17+2*i+1),8'(s*17+2*i)}))
                    $fatal(1,"RMW corrupted s=%0d i=%0d=%h",s,i,result);
            end
        end
        // Deleted-data writes change the metadata as well as the payload.
        access(22'o17777000,0,0);
        for(integer i=0;i<64;i++)access(22'o17777002,1,16'hb000+i);
        sector_id(8);
        if($test$plusargs("RAW"))begin
            integer before_writes;
            before_writes=card.write_commands;
            access(22'o17777000,1,16'o37);
            do access(22'o17777000,0,0);while(!result[7]);
            if((result&16'hfc00)!=16'hc000 || card.write_commands!=before_writes)
                $fatal(1,"raw deleted write must reject before touching SD");
            read_sector(8);
            for(integer i=0;i<64;i++)begin
                access(22'o17777002,0,0);
                if(result!={8'(8*17+2*i+1),8'(8*17+2*i)})$fatal(1,"raw rejected write changed data");
            end
            // Track 1 begins halfway through an SD block: check its first
            // sectors after a write to prove the byte address includes track skew.
            access(22'o17777000,1,16'o25);complete();
            for(integer n=0;n<5;n++)read_sector(1);
            for(integer i=0;i<64;i++)begin
                access(22'o17777002,0,0);
                if(result!={8'(17+2*i+1),8'(17+2*i)})$fatal(1,"unaligned raw track payload");
            end
            $display("PASS raw QX MBR/FAT16 mount, RMW, unaligned track and deleted-write rejection");$finish;
        end
        access(22'o17777000,1,16'o37);complete();
        sector_id(8);access(22'o17777000,1,16'o33);
        do access(22'o17777000,0,0);while(!result[7]);
        if((result&16'hfc00)!=16'h8400)$fatal(1,"deleted write metadata");
        for(integer i=0;i<64;i++)begin access(22'o17777002,0,0);if(result!=16'hb000+i)$fatal(1,"deleted write payload");end
        $display("PASS real SERV/SD QX RMW, deleted/CRC/absent flags and deleted writes");$finish;
    end
    initial begin #1000000000;$fatal(1,"disk integration timeout");end
endmodule
