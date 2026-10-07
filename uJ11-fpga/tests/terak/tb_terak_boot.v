`timescale 1ns/1ps
// Cold reset -> real ROM -> J11 microcode -> SERV -> wire-level SDHC -> UCSD.
// No guest PC/register patching and no host disk handler is used.
module tb_terak_boot #(parameter DISPLAY_EBR=0,RAM_BYTES=57344,BOOT_BIOS=0,MECHANICAL_SPEEDUP=1,
    parameter MICROCODE="build/hc7000-terak/cpu/microcode.mem");
    reg clk=0,vclk=0,initial_reset=1,power_on=1,button_n=1;
    wire button_reset,button_cold,storage_idle;
    wire reset=initial_reset || (BOOT_BIOS && button_reset);
    uj11_terak_button #(.SAMPLE_DIVISOR(10),.MEDIUM_SAMPLES(5),.LONG_SAMPLES(20)) button(
        .clk(clk),.power_on(power_on),.button_n(button_n),.storage_idle(storage_idle),
        .guest_reset(button_reset),.cold_reset(button_cold));
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
    // Optional acceleration changes only QX mechanical waits. CPU, SPI,
    // SERV timers, BEVNT and video retain their real rates.
    uj11_terak_board #(.QX_MECHANICAL_SPEEDUP(MECHANICAL_SPEEDUP),.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),
        .BOOT_BIOS(BOOT_BIOS),.MICROCODE(MICROCODE),.CLEAR_WORDS(36864)) dut(
        .clk(clk),.reset(reset),.cold_reset(initial_reset || (BOOT_BIOS && button_cold)),.storage_idle(storage_idle),.power_on(power_on),
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
    reg [7:0] bios_font[0:959];
    task check_bios_font;
        string filename;
        if(!$value$plusargs("BIOS_FONT=%s",filename))$fatal(1,"BIOS font reference required");
        $readmemh(filename,bios_font);
        for(integer code=32;code<128;code++)for(integer scan=0;scan<10;scan++)
            if(screen_storage.font_byte(code,scan)!==bios_font[(code-32)*10+scan])
                $fatal(1,"BIOS glyph mismatch: ASCII=%0d scan=%0d actual=%h expected=%h",
                    code,scan,screen_storage.font_byte(code,scan),bios_font[(code-32)*10+scan]);
    endtask
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
    function automatic bit rt11_prompt;
        rt11_prompt=0;
        for(integer row=0;row<25;row++)
            if(screen_storage.page_byte(row,0)==8'h2e && screen_storage.page_byte(row,1)==8'h7f)
                rt11_prompt=1;
    endfunction
    task ps2_byte(input [7:0] data);
        reg [10:0] frame;
        frame={1'b1,~^data,data,1'b0};
        for(integer bit_number=0;bit_number<11;bit_number++)begin
            wait(ps2_clock);key_data=frame[bit_number];#20000;key_clock=0;#20000;key_clock=1;
        end
        key_data=1;#40000;
    endtask
    task check_resets(input bit rt11);
        integer fonts_before,rom_before;
        reg [3:0] present_before;
        present_before=dut.bus.qx.present;
        rom_before=rom_fetches;
        button_n=0;repeat(20)@(negedge clk);button_n=1;
        repeat(80)@(negedge clk);
        if(reset || !dut.cpu_start || dut.bus.boot_menu || rom_fetches!=rom_before)
            $fatal(1,"short reset changed guest state");
        fonts_before=font_writes;rom_before=rom_fetches;
        button_n=0;repeat(85)@(negedge clk);button_n=1;
        repeat(80)@(negedge clk);
        if(reset || !dut.cpu_start || dut.bus.boot_menu || dut.bus.qx.present!=present_before)
            $fatal(1,"medium reset lost selected media or restarted SERV");
        for(integer n=0;n<400;n++)begin
            #50000000;
            if(font_writes>fonts_before && rom_fetches>rom_before &&
                (rt11 ? rt11_prompt() && (has_text("RT-11SJ") || has_text("RT-11FB")) :
                        font_writes>=fonts_before+1920 && has_text("COMMAND:")))break;
        end
        if(font_writes<=fonts_before || !(rt11 ? rt11_prompt() : has_text("COMMAND:")))
            $fatal(1,"warm guest reboot failed");
        fonts_before=font_writes;
        button_n=0;repeat(240)@(negedge clk);button_n=1;
        for(integer n=0;n<400;n++)begin
            #1000000;
            if(dut.bus.boot_menu && font_writes>fonts_before && has_text("PRESS 1-4"))break;
        end
        if(!dut.bus.boot_menu || !has_text("SELECT BOOT"))$fatal(1,"long reset did not return to BIOS");
        check_bios_font();
        $display("PASS BIOS menu, rejected configuration, selection, short/medium/long Reset and preserved media");
    endtask
    string snapshot;
    reg passed=0;
    initial begin: boot_test
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);initial_reset=0;
        wait(dut.cpu_start);$display("SERV ready at %0t",$time);
        if(BOOT_BIOS)begin: choose_boot
            integer choice;
            choice=2;
            if($value$plusargs("BIOS_CHOICE=%d",choice))begin end
            for(integer n=0;n<100;n++)begin
                #1000000;
                if(has_text("PRESS 1-4"))break;
            end
            if(!has_text("SELECT BOOT") || !dut.bus.boot_menu)$fatal(1,"primary BIOS did not reach native menu");
            check_bios_font();
            if($value$plusargs("BIOS_SCREEN_MEMORY=%s",snapshot))begin
                save_screen(snapshot);
                $display("BIOS_SCREEN GAR=%0d VIR=%0d VCR=%0d",dut.bus.gar,dut.bus.vir,dut.bus.vcr);
            end
            if($test$plusargs("BIOS_MENU_ONLY"))begin
                $display("PASS primary BIOS font and menu");$finish;
            end
            screen();$fflush();
            if($test$plusargs("BIOS_BAD_CHOICE"))begin
                ps2_byte(8'h26); // 3, intentionally absent TERAK3.CFG
                // A shared FAT root can contain many more files than the
                // isolated test card. Wait for the real rejection response.
                for(integer n=0;n<100;n++)begin
                    #1000000;
                    if(has_text("UNAVAILABLE"))break;
                end
                if(!has_text("UNAVAILABLE") || !dut.bus.boot_menu)$fatal(1,"bad BIOS configuration escaped the menu");
            end
            ps2_byte(choice==1 ? 8'h16 : choice==2 ? 8'h1e : choice==3 ? 8'h26 : 8'h25);
            if($test$plusargs("BIOS_ONLY"))begin
                #200000000;
                $display("BIOS PC=%o menu=%b request=%b/%b addr=%o ready/error=%b/%b SERV ia=%h da=%h pending=%b",
                    dut.cpu.pc,dut.bus.boot_menu,dut.request,dut.writing,dut.address,dut.ready,dut.error,
                    dut.bus.disk.ia,dut.bus.disk.da,dut.bus.disk.bridge.reset_pending);
                if(dut.bus.boot_menu)$fatal(1,"BIOS selection did not switch to native ROM");
                $display("PASS BIOS selection into native ROM");$finish;
            end
        end
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
        end else if($test$plusargs("RT11"))begin
            for(integer n=0;n<400;n++)begin
                #50000000;
                if(n%20==0)begin
                    $display("RT11 PC=%o PSW=%o font=%0d reads=%0d",dut.cpu.pc,dut.cpu.psw,font_writes,card.read_count);
                    screen();$fflush();
                end
                if((has_text("RT-11SJ") || has_text("RT-11FB")) && rt11_prompt())begin
                    if(font_writes<(BOOT_BIOS ? 2880 : 1920))$fatal(1,"RT-11 skipped native CHRSET.SYS loading");
                    #20000000;
                    screen();
                    ps2_byte(8'h23);ps2_byte(8'h43);ps2_byte(8'h2d);ps2_byte(8'h5a); // DIR <return>
                    for(integer k=0;k<200;k++)begin
                        #50000000;
                        if(has_text("FREE BLOCKS"))begin passed=1;break;end
                    end
                    break;
                end
            end
            screen();
            if(!passed)$fatal(1,"native TERAK RT-11 boot/DIR failed PC=%o PSW=%o",dut.cpu.pc,dut.cpu.psw);
            if(!rom_fetches || !card.read_count)$fatal(1,"RT-11 bypassed ROM/SD path");
            // The resident TK handler blanks text while updating a row.
            // Capture only after it restores display enable; never force VCR.
            wait(dut.bus.vcr[2:0]!=0 && !dut.bus.vcr[7]);
            if($value$plusargs("SCREEN_MEMORY=%s",snapshot))save_screen(snapshot);
            $display("SCREEN GAR=%0d VIR=%0d VCR=%0d",dut.bus.gar,dut.bus.vir,dut.bus.vcr);
            if(BOOT_BIOS)check_resets(1);
            $display("PASS native TERAK RT-11 ROM/SD boot and PS2 DIR command");$finish;
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
            if(BOOT_BIOS)check_resets(0);
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
    initial begin #45000000000;$fatal(1,"boot timeout");end
endmodule
