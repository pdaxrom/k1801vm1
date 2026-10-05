`timescale 1ns/1ps
// BIOS navigation through real PS/2 frames, SRAM pins and native VM1.
module tb_bios_keyboard;
    reg clk = 0, clk48 = 0, video_clk = 0, power_on = 1, button_n = 1;
    always #20.833333 clk = !clk;
    always #10.416667 clk48 = !clk48;
    always #7.8125 video_clk = !video_clk;
    reg ps2_low = 0, ps2_data = 1;
    tri1 ps2_clock;
    assign ps2_clock = ps2_low ? 1'b0 : 1'bz;
    wire [19:0] sram_address;
    wire [15:0] sram_data;
    wire ce, oe, we, lb, ub, sd_cs, sd_sck, sd_mosi, sd_miso;
    wire [5:0] tvout;
    wire [1:0] audio;
    wire tx, initialized, locked, error, underrun;
    wire [7:0] stage;
    wire [8:0] high_digit, low_digit;
    integer reads, i;
    reg [7:0] previous_stage = 0;
    // Only the physical button's long hold is shortened. BIOS countdown and
    // all CPU/PS2/SRAM/PAL clocks have their production values.
    bk_computer #(.PANEL_CLOCK_HZ(32000)) computer(
        clk,clk48,video_clk,power_on,button_n,ps2_clock,ps2_data,
        sram_address,sram_data,ce,oe,we,lb,ub,sd_cs,sd_sck,sd_mosi,sd_miso,
        tvout,audio,tx,high_digit,low_digit,initialized,locked,error,underrun,stage);
    sram_model ram(sram_address,sram_data,ce,oe,we,lb,ub);
    sd_spi_model card(sd_cs,sd_sck,sd_mosi,sd_miso,reads);
    wire menu_poll = computer.external_request && computer.external_ready &&
                    !computer.external_write_enable && computer.external_address == 16'o177660 && stage == 8'h11;
    wire [15:0] cursor = ram.memory[16'o1546 >> 1];
    wire [7:0] selected_model = ram.memory[16'o1410 >> 1][15:8];
    wire [7:0] selected_config = ram.memory[16'o1412 >> 1][7:0];
    always @(posedge clk) begin
        if(stage != previous_stage) begin
            $display("BIOS PS2 stage=%02h time_ms=%0d reads=%0d",stage,$time/1000000,reads);
            $fflush(); previous_stage <= stage;
        end
        if(error) $fatal(1,"BIOS boot error=%02h",stage);
        if(underrun) $fatal(1,"BIOS PAL underrun");
        if($test$plusargs("countdown-arrow") && stage == 8'h04)
            $fatal(1,"BIOS ignored physical arrow during countdown and auto-booted saved model");
    end
    task bit_send(input value);
        begin ps2_data = value; #15000; ps2_low = 1; #30000; ps2_low = 0; #15000; end
    endtask
    task scan(input [7:0] value);
        integer bit_number;
        begin
            wait(ps2_clock); #10000; bit_send(0);
            for(bit_number=0;bit_number<8;bit_number=bit_number+1) bit_send(value[bit_number]);
            bit_send(~^value); bit_send(1); ps2_data = 1; #50000;
        end
    endtask
    task key(input [7:0] value, input extended);
        begin
            wait(menu_poll && !computer.bus.key_pending && !computer.key_valid);
            if(extended) scan(8'he0);
            scan(value);
            if(extended) scan(8'he0);
            scan(8'hf0); scan(value);
            wait(!computer.bus.key_pending && !computer.key_valid && !computer.scan_value[8]);
            // Wait for the ensuing redraw to complete and return to polling.
            repeat(100) @(negedge clk);
            wait(menu_poll);
        end
    endtask
    initial begin
        if($test$plusargs("turbo-bios")) force computer.clock_request = 3'd6;
        for(i=0;i<131072;i=i+1) ram.memory[i] = 16'ha5a5;
        #300; power_on = 0;
        wait(!computer.hard_reset);
        wait(stage == 8'h10);
        if($test$plusargs("countdown-arrow")) begin
            // The already visible menu advertises arrows. The first Right
            // must cancel auto-boot and select the next model, without loss.
            scan(8'he0); scan(8'h74); scan(8'he0); scan(8'hf0); scan(8'h74);
            wait(!computer.bus.key_pending && !computer.key_valid && !computer.scan_value[8]);
            repeat(100000) @(negedge clk);
            if(stage == 8'h10) $fatal(1,"BIOS consumed physical arrow but ignored it during countdown");
        end else begin
            scan(8'h66); scan(8'hf0); scan(8'h66); // Backspace, not Delete.
        end
        wait(stage == 8'h11); wait(menu_poll);
        if(selected_model != ($test$plusargs("countdown-arrow") ? 4 : 3) || cursor != 0)
            $fatal(1,"Saved model/menu entry/first arrow");
        $display("PASS saved model 3: physical Backspace opens BIOS"); $fflush();
        if($test$plusargs("countdown-arrow")) key(8'h6b,1); // Return to saved model 3.
        key(8'h74,1); if(selected_model != 4) $fatal(1,"Right did not select model 4: %0d",selected_model);
        if($test$plusargs("native-smoke")) begin
            key(8'h6b,1);
            if(selected_model != 3 || card.writes != 0) $fatal(1,"Stock-clock arrow round trip");
            $display("PASS stock 3 MHz BIOS: physical menu entry and Left/Right; no SD writes");
            $finish;
        end
        key(8'h74,1); if(selected_model != 0 || selected_config != 0) $fatal(1,"Model wrap/default 3 MHz");
        key(8'h6b,1); if(selected_model != 4 || selected_config != 1) $fatal(1,"Left wrap/default 4 MHz");
        key(8'h74,1); key(8'h74,1); key(8'h74,1);
        if(selected_model != 2 || selected_config != 0) $fatal(1,"Physical model selection");
        $display("PASS physical Left/Right: all five models, wrap and stock frequency"); $fflush();
        key(8'h72,1); if(cursor != 1) $fatal(1,"Down did not move cursor");
        key(8'h74,1); if(selected_config != 1) $fatal(1,"Frequency right");
        key(8'h6b,1); if(selected_config != 0) $fatal(1,"Frequency left");
        key(8'h72,1); key(8'h74,1); if(selected_config != 8) $fatal(1,"Color toggle");
        key(8'h74,1); if(selected_config != 0) $fatal(1,"Mono toggle");
        key(8'h75,1); if(cursor != 1) $fatal(1,"Up did not move cursor");
        repeat(7) key(8'h72,1); // Boot without save.
        if(cursor != 8 || card.writes != 0) $fatal(1,"Boot row/SD unchanged");
        wait(menu_poll); scan(8'h5a); scan(8'hf0); scan(8'h5a);
        wait(locked);
        if(computer.model != 2 || computer.boot_frequency != 0 || card.writes != 0)
            $fatal(1,"Enter did not boot selected model without writing SD");
        $display("PASS physical Up/Down, frequency/video editing, Enter boots model 2; no SD writes");
        $finish;
    end
    initial begin
        #30000000000;
        $fatal(1,"BIOS PS2 timeout stage=%h cursor=%0d selected=%0d addr=%06o valid=%b pending=%b scans=%0d inhibit=%b",
               stage,cursor,selected_model,computer.cpu_address,computer.key_valid,computer.bus.key_pending,
               computer.transport.level,computer.transport.inhibit);
    end
endmodule
