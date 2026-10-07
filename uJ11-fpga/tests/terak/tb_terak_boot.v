`timescale 1ns/1ps
// Cold reset -> real ROM -> J11 microcode -> SERV -> wire-level SDHC -> UCSD.
// No guest PC/register patching and no host disk handler is used.
module tb_terak_boot #(parameter DISPLAY_EBR=0,RAM_BYTES=57344,parameter MICROCODE="build/hc7000-terak/cpu/microcode.mem");
    reg clk=0,vclk=0,reset=1,power_on=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    tri1 ps2_clock;
    reg key_clock=1,key_data=1;
    assign ps2_clock=key_clock ? 1'bz : 1'b0;
    wire initialized;
    wire [5:0] dac;
    wire [19:0] sa;
    wire [15:0] sd;
    wire ce,oe,we,lb,ub,cs,sck,mosi,miso;
    uj11_terak_board #(.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.MICROCODE(MICROCODE),.CLEAR_WORDS(36864)) dut(.clk(clk),.reset(reset),.power_on(power_on),
        .uart_rx(1'b1),.halt_button(1'b0),.video_clk(vclk),.video_reset(1'b0),
        .ps2_clock(ps2_clock),.ps2_data(key_data),.tvout(dac),.audio(),.memory_initialized(initialized),
        .uart_tx(),.panel_keys(4'b0),.panel_pins(),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(),.stopped(),
        .diagnostic_halt(),.diagnostic_wait(),.diagnostic_retire(),.diagnostic_mode());
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model #(.SECTORS(256)) card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),
        .absent(1'b0),.fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),
        .bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
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
    serv_memory_guard guard(.clk(clk),.reset(dut.bus.disk.iop_reset),
        .write(dut.bus.disk.data_accept && dut.bus.disk.memory_selected && dut.bus.disk.de && dut.bus.disk.da<(DISPLAY_EBR ? 14848 : 18944)),
        .address(dut.bus.disk.da));
    integer text_writes=0,font_writes=0,rom_fetches=0,qx_commands=0;
    integer white_pixels=0;
    always @(posedge vclk)if(!reset && dac==50)white_pixels++;
    reg key_consumed=0;
    always @(posedge clk)if(!reset)begin
        if(dut.request && dut.ready && !dut.writing && dut.bus.console_selected &&
            dut.bus.offset==13'o17562 && dut.read_data==16'h66)key_consumed<=1;
        if(dut.request && dut.ready && !dut.writing && dut.bus.rom_selected)rom_fetches++;
        if(dut.request && dut.ready && dut.writing && dut.bus.aperture)begin
            if(dut.bus.vcr[7])font_writes++;else text_writes++;
        end
        if(dut.bus.qx.request && !dut.bus.qx.seen && dut.writing && !dut.address[1] && dut.write_data[0])begin
            qx_commands++;
            if(qx_commands<12 || qx_commands%100==0)
                $display("QX cmd %0d: %o track=%0d sector=%0d PC=%o",qx_commands,dut.write_data,
                    dut.bus.qx.track[0],dut.bus.qx.sector,dut.cpu.pc);
        end
    end
    generate if(DISPLAY_EBR)begin: screen_storage
        function automatic [7:0] page_byte(input integer row,col);
            integer packed_index;
            begin
                packed_index=row*80+col;
                page_byte=packed_index<1024 ? dut.bus.display_ebr.memory.banks[0].bytes[packed_index] :
                    dut.bus.display_ebr.memory.banks[1].bytes[packed_index-1024];
            end
        endfunction
        function automatic [7:0] font_byte(input integer code,scan);
            integer packed_index;
            begin
                packed_index=((code&127)-32+(code>=128 ? 96 : 0))*10+scan;
                font_byte=packed_index<1024 ? dut.bus.display_ebr.memory.banks[2].bytes[packed_index] :
                    dut.bus.display_ebr.memory.banks[3].bytes[packed_index-1024];
            end
        endfunction
    end else begin: screen_storage
        function automatic [7:0] page_byte(input integer row,col);
            page_byte=ram.memory[65536+row*128+col];
        endfunction
        function automatic [7:0] font_byte(input integer code,scan);
            font_byte=ram.memory[69632+code*16+scan];
        endfunction
    end endgenerate
    reg [7:0] snapshot_memory[0:73727];
    task save_screen(input string filename);
        for(integer i=0;i<73728;i++)snapshot_memory[i]=ram.memory[i];
        for(integer row=0;row<25;row++)for(integer col=0;col<80;col++)
            snapshot_memory[65536+row*128+col]=screen_storage.page_byte(row,col);
        for(integer code=32;code<256;code++)if(code<128 || code>=160)
            for(integer scan=0;scan<10;scan++)
                snapshot_memory[69632+code*16+scan]=screen_storage.font_byte(code,scan);
        $writememh(filename,snapshot_memory);
    endtask
    task screen;
        for(integer row=0;row<25;row++)begin
            $write("%02d ",row);
            for(integer col=0;col<80;col++)begin
                if((screen_storage.page_byte(row,col)&127)>=32)
                    $write("%c",screen_storage.page_byte(row,col)&127);
                else $write(" ");
            end
            $write("\n");
        end
    endtask
    function automatic bit has_text(input string needle);
        bit matched;
        reg [7:0] ch;
        has_text=0;
        for(integer row=0;row<25;row++)for(integer col=0;col<=80-needle.len();col++)begin
            matched=1;
            for(integer i=0;i<needle.len();i++)begin
                ch=screen_storage.page_byte(row,col+i);
                if(ch>=97 && ch<=122)ch=ch-32;
                if(ch!=needle[i])matched=0;
            end
            if(matched)has_text=1;
        end
    endfunction
    task ps2_byte(input [7:0] data);
        reg [10:0] frame;
        frame={1'b1,~^data,data,1'b0};
        for(integer bit_number=0;bit_number<11;bit_number++)begin
            wait(ps2_clock);key_data=frame[bit_number];#20000;key_clock=0;#20000;key_clock=1;
        end
        key_data=1;#40000;
    endtask
    string snapshot;
    reg passed=0;
    initial begin: boot_test
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait(dut.cpu_start);$display("SERV ready at %0t",$time);
        if($test$plusargs("DDBOOT"))begin
            if(!dut.bus.boot_qb)$fatal(1,"DD partition did not select QB ROM");
            for(integer n=0;n<100;n++)begin
                #10000000;
                if(ram.memory[32768]==8'haa && ram.memory[32769]==8'haa)begin
                    if({ram.memory[1],ram.memory[0]}!=16'o10005)$fatal(1,"QB media identifier");
                    if(qx_commands || !rom_fetches || !card.read_count)$fatal(1,"QB boot bypassed native path");
                    $display("PASS QB cold mixed-density DMA boot and BPT handler, both sides, running count/address");
                    passed=1;break;
                end
                if(ram.memory[32768]==8'h55 && ram.memory[32769]==8'h55)$fatal(1,"QB softboot assertions failed");
            end
            if(passed)begin
                $finish;
            end else begin
                $fatal(1,"QB cold boot timeout PC=%o R0/R1/R2/R3=%o/%o/%o/%o",dut.cpu.pc,
                    dut.cpu.dp.rf.words[0],dut.cpu.dp.rf.words[1],dut.cpu.dp.rf.words[2],dut.cpu.dp.rf.words[3]);
            end
        end else begin
        for(integer n=0;n<400;n++)begin
            #50000000;
            $display("BOOT t=%0t PC=%o PSW=%o text=%0d font=%0d SDreads=%0d",$time,dut.cpu.pc,dut.cpu.psw,
                text_writes,font_writes,card.read_count);
            $fflush();
            if(font_writes>=1920 && has_text("COMMAND:") && has_text("U.C.S.D."))begin
                // Allow a complete PAL field with native text and graphics enabled.
                #20000000;
                if(dut.bus.underruns)$fatal(1,"video underruns under CPU/SD load: %d",dut.bus.underruns);
                if(!white_pixels)$fatal(1,"native DAC did not display the UCSD menu");
                screen();
                if($value$plusargs("SCREEN_MEMORY=%s",snapshot))save_screen(snapshot);
                $display("SCREEN GAR=%0d VIR=%0d VCR=%0d",dut.bus.gar,dut.bus.vir,dut.bus.vcr);
                ps2_byte(8'h2b); // F: selects the UCSD filer through the real keyboard path.
                for(integer k=0;k<100 && !key_consumed;k++)#1000000;
                if(!key_consumed)$fatal(1,"guest did not consume PS/2 F through KDR");
                passed=1;break;
            end
        end
        if(passed)begin
            $display("PASS Terak cold ROM/SD UCSD II.0 menu, native font/video and PS2 guest input");$finish;
        end else begin
        screen();
        for(integer r=0;r<8;r++)$display("R%0d=%o",r,dut.cpu.dp.rf.words[r]);
        $display("console KSR=%b ESR=%b IE=%b IRQ=%b vector=%o latch=%h GAR/VIR/VCR=%o/%o/%o",dut.bus.console.key_done,
            dut.bus.console.output_done,dut.bus.console.output_ie,dut.bus.console.irq,dut.bus.console.vector,
            dut.bus.console.output_latch,dut.bus.gar,dut.bus.vir,dut.bus.vcr);
        $display("QX ctl=%o done=%b irq=%b present=%b backend=%b",dut.bus.qx.control,dut.bus.qx.done,dut.bus.qx.irq,
            dut.bus.qx.present,dut.bus.qx.backend);
        $fatal(1,"UCSD boot did not reach native console");
        end
        end
    end
    initial begin #22000000000;$fatal(1,"boot timeout");end
endmodule
