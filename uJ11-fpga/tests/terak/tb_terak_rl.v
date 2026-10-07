`timescale 1ns/1ps
// Real SERV and SD card, with an independent bus master checking 256-byte RMW.
module tb_terak_rl #(parameter DISPLAY_EBR=1,RAM_BYTES=253952);
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
    uj11_terak_bus #(.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.CLEAR_WORDS(1),.BOOT_BIOS(1),.RL_ENABLE(1)) bus(.clk(clk),.reset(reset),.cold_reset(reset),.power_on(power_on),
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
    task access(input [21:0] a,input bit w,input [15:0] data);
        @(negedge clk);address=a;writing=w;wd=data;request=1;
        do @(negedge clk);while(!ready);
        if(error)$fatal(1,"unexpected bus error at %o",a);
        result=rd;request=0;repeat(2)@(negedge clk);
    endtask
    localparam CSR=22'o17774400;
    task complete(input integer expected_error=0);
        do access(CSR,0,0);while(!result[7]);
        if(result[15]!=!!expected_error || expected_error && result[13:10]!=expected_error)
            $fatal(1,"RL completion CSR=%o expected error=%0d",result,expected_error);
    endtask
    task seek(input integer cylinder,input integer head=0,input integer unit=0);
        access(CSR+4,1,16'o13);access(CSR,1,16'(unit*256+4));complete();
        access(CSR,1,16'(unit*256+8));complete();access(CSR+6,0,0);
        access(CSR+4,1,(result&16'hff80)|1);access(CSR,1,16'(unit*256+6));complete();
        access(CSR+4,1,16'((cylinder<<7)|(head<<4)|5));
        access(CSR,1,16'(unit*256+6));complete();
    endtask
    task transfer(input integer fn,input integer cylinder,input integer head,input integer sec,
                  input integer address,input integer words,input integer expected=0,input integer unit=0);
        seek(cylinder,head,unit);
        access(CSR+2,1,16'(address));access(CSR+4,1,16'((cylinder<<7)|(head<<6)|sec));
        access(CSR+6,1,16'(-words));access(CSR,1,16'(unit*256|((address>>12)&48)|fn*2));complete(expected);
    endtask
    function automatic [15:0] pattern(input integer c,h,s,i);
        integer block,j;
        begin
            block=(c*2+h)*20+(s>>1);j=(s&1)*256+i*2;
            pattern={8'((block/20*17)^(block%20*11)^((j+1)*3)^((j+1)>>8)),
                     8'((block/20*17)^(block%20*11)^(j*3)^(j>>8))};
        end
    endfunction
    task check(input integer a,c,h,s,count=128);
        for(integer i=0;i<count;i++)begin
            access(22'(a+i*2),0,0);
            if(result!=pattern(c,h,s,i))$fatal(1,"RL data %0d/%0d/%0d[%0d]=%h",c,h,s,i,result);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;wait(cpu_start);
        access(22'o17771020,1,5);access(22'o17771020,0,0);
        if(result || !bus.boot_rl || bus.boot_menu)$fatal(1,"RL BIOS configuration failed");
        access(CSR,0,0);if((result&16'o201)!=16'o201)$fatal(1,"RL01 not ready CSR=%o",result);
        seek(0);access(CSR+6,0,0); // Header FIFO is independently consumed by seek.
        access(CSR+4,1,16'o13);access(CSR,1,4);complete();access(CSR+6,0,0);
        if(result!=16'o35)$fatal(1,"RL01 status %o",result);
        access(CSR+4,1,16'o13);access(CSR,1,16'o404);complete();access(CSR+6,0,0);
        if((result&16'o20200)!=16'o20200)$fatal(1,"RL02/read-only status %o",result);
        // Cross 64 KiB with physical 18-bit DMA and verify BAE/WC updates.
        transfer(6,3,1,6,'h1ff00,256);
        check('h1ff00,3,1,6);check('h20000,3,1,7);
        access(CSR+2,0,0);if(result!=16'h100)$fatal(1,"RL BAR wrap %h",result);
        access(CSR,0,0);if((result&48)!=32)$fatal(1,"RL BAE carry %o",result);
        access(CSR+6,0,0);if(result)$fatal(1,"RL word count %o",result);
        // Partial write of an odd 256-byte sector preserves the preceding half.
        for(integer i=0;i<7;i++)access(22'('h21000+i*2),1,16'ha000+i);
        transfer(5,3,1,7,'h21000,7);
        transfer(6,3,1,6,'h22000,256);check('h22000,3,1,6);
        for(integer i=0;i<128;i++)begin
            access(22'('h22100+i*2),0,0);
            if(result!=(i<7 ? 16'ha000+i : 0))$fatal(1,"RL partial write/padding %0d=%h",i,result);
        end
        transfer(1,3,1,6,'h22000,256);
        access(22'h22000,1,16'hffff);transfer(1,3,1,6,'h22000,1,2);
        transfer(5,0,0,0,'h21000,7,1,1);
        if(card.write_commands!=1)$fatal(1,"read-only RL disk was written");
        transfer(6,0,0,40,'h22000,128,5);
        transfer(6,0,0,0,'h3e000,128,8);
        // CRDY+IE produces a BR5 interrupt; acknowledge retains the CSR IE.
        access(CSR,1,16'o300);
        wait(bus.qb_irq);
        while(priority_level==6)begin @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;end
        if(priority_level!=5 || vector!=16'o160)$fatal(1,"RL interrupt vector/priority");
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
        wait(!bus.qb_irq);repeat(2000)@(negedge clk);
        access(CSR,0,0);if(!result[6])$fatal(1,"RL acknowledge cleared IE");
        // INIT cancels an outstanding SD read without losing the selected image.
        seek(12);access(CSR+2,1,0);access(CSR+4,1,16'(12<<7));access(CSR+6,1,-256);
        access(CSR,1,12);wait(bus.disk.engine_busy);
        @(negedge clk);peripheral_reset=1;repeat(4)@(negedge clk);peripheral_reset=0;
        repeat(100000)@(negedge clk);
        access(CSR,0,0);if(result!=16'o201 || !bus.boot_rl)$fatal(1,"RL INIT lost media/state");
        if(card.write_commands!=1)$fatal(1,"reset committed an unintended disk write");
        $display("PASS TERAK RL01/RL02 MBR, 18-bit DMA, half-sector RMW, compare, RO, NXM, IRQ and INIT");
        $finish;
    end
    initial begin #3000000000;$fatal(1,"RL test timeout");end
endmodule
