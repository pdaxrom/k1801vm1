`timescale 1ns/1ps
// Genuine CPU-executed cold boot: only the resident EBR has initial contents.
module tb_sd_boot;
`ifdef BK_VM1_VENDOR_RAM
    GSR GSR_INST(1'b1);
    PUR PUR_INST(1'b1);
`endif
    parameter HIGH_CAPACITY = 1, VERSION2 = 1;
    reg fast_cpu_clk = 0, clk48 = 0, sys_clk = 0, reset = 1, power_on = 1;
    integer native_rate=0;
    integer runtime_mode=-1;
    wire cpu_clk,native_cpu_clk,native_cpu_n,cpu_stall;
    wire [2:0] native_frequency;
    assign cpu_clk=native_rate?native_cpu_clk:fast_cpu_clk;
    reg boot_reset = 0, seen_locked = 0;
    wire machine_reset = reset || boot_reset;
    reg [2:0] cpu_reset_pipe = 7;
    always @(posedge cpu_clk or posedge machine_reset)
        if(machine_reset) cpu_reset_pipe <= 7;
        else cpu_reset_pipe <= {cpu_reset_pipe[1:0],1'b0};
    wire cpu_reset = cpu_reset_pipe[2];
    reg quiesce = 0;
    wire storage_idle;
    reg [1:0] init_sync = 3, ack_sync = 0;
    reg [2:0] irq_sync = 0;
    reg [1:0] physical_stop_sync = 0, physical_held_sync = 0;
    reg ack_toggle = 0, ack_previous = 0, ack_seen = 0;
    always @(posedge cpu_clk or posedge cpu_reset) begin
        if(cpu_reset) begin
            irq_sync <= 0; ack_toggle <= 0; ack_previous <= 0;
            physical_stop_sync <= 0; physical_held_sync <= 0;
        end
        else begin
            irq_sync <= {irq_sync[1:0],irq};
            physical_stop_sync <= {physical_stop_sync[0],physical_stop};
            physical_held_sync <= {physical_held_sync[0],physical_held};
            ack_previous <= irq_ack;
            if(irq_ack && !ack_previous) ack_toggle <= !ack_toggle;
        end
    end
    always @(posedge sys_clk) begin
        if(machine_reset) begin init_sync <= 3; ack_sync <= 0; ack_seen <= 0; end
        else begin
            init_sync <= {init_sync[0],peripheral_reset};
            ack_sync <= {ack_sync[0],ack_toggle}; ack_seen <= ack_sync[1];
        end
    end
    // Accelerated CPU still finishes the high phase and honours disk waits.
    // Native-rate runs use the production phase generator below.
    always #10 if(fast_cpu_clk || !cpu_stall) fast_cpu_clk = !fast_cpu_clk;
    always #10.416667 clk48 = !clk48;
    always #17 sys_clk = !sys_clk;
    wire [15:0] cpu_address, cpu_data, cpu_read, address, write_data, read_data;
    wire [1:0] cpu_lanes, lanes;
    wire cycle, strobe, cpu_write, cpu_ack, cpu_busy, request, write, ready;
    wire irq, irq_ack, peripheral_reset, sys_write, sys_read, ext_write;
    wire [15:0] vector, scroll;
    wire locked, error;
    wire [7:0] stage;
    wire mem_request, mem_write;
    wire [19:0] mem_address;
    wire [15:0] mem_data, boot_data;
    wire [1:0] mem_lanes;
    reg mem_ready = 0;
    reg [15:0] mem_read = 0;
    reg [15:0] ram[0:131071];
    reg key_valid = 0, entered = 0;
    reg stop_pressed = 0;
    reg [7:0] key_code = 8'h0a;
    integer bios_action = 0, bios_error = 0, expected_model = 2;
    wire [2:0] model;
    reg [15:0] cpu_bank = 0;
    wire [15:0] external_bank;
    wire key_ready;
    reg physical_stop_test = 0, ps2_low = 0, ps2_data = 1;
    tri1 ps2_clock;
    assign ps2_clock = ps2_low ? 1'b0 : 1'bz;
    wire [8:0] physical_scan;
    wire physical_pop, physical_valid, physical_held, physical_stop;
    wire [7:0] physical_code;
    uj11_ps2_keyboard #(.CLOCK_HZ(29411764)) transport(sys_clk,machine_reset || init_sync[1],
        ps2_clock,ps2_data,physical_pop,physical_scan);
    bk_keymap keymap(sys_clk,machine_reset || init_sync[1],physical_scan[8],physical_scan[7:0],
        physical_pop,physical_valid,physical_code,key_ready,physical_held,physical_stop,!locked);
    wire sd_cs, sd_sck, sd_mosi, sd_miso;
    integer reads;
    integer cycles = 0, writes = 0, polls = 0, i;
    reg [7:0] sys_input = 8'o370;
    reg [7:0] sys_output = 8'o220;
    wire mono_audio;
    bk_audio sound(cpu_clk,clk48,cpu_reset || peripheral_reset,ext_write,cpu_lanes,cpu_data,
                   sys_output[6],mono_audio);
    reg ay_music = 0;
    integer player_writes = 0, stop_trace = 0;
    reg stop_started = 0;
    always @(posedge clk48) if(sound.command_valid && !sound.command[8]) player_writes <= player_writes + 1;
    integer ay_commands = 0, ay_samples = 0, audio_divider = 0, audio_file = 0, ay_trace = 0;
    integer ay_min = 511, ay_max = 0;
    always @(posedge clk48) if(ay_music && !sound.audio_reset) begin
        if(sound.command_valid && !sound.command[8]) begin
            ay_commands = ay_commands + 1;
            $fdisplay(ay_trace,"%0t %0d %0d",$time,sound.selected,sound.command[7:0]);
            if(ay_commands == 1) begin
                $display("AY player first data command: time=%0t register=%0d",$time,sound.selected);
                $fflush();
            end
        end
        if(ay_commands != 0) begin
            if(audio_divider == 999) begin
                audio_divider = 0; ay_samples = ay_samples + 1;
                $fdisplay(audio_file,"%0d",sound.mixed);
                if(sound.mixed < ay_min) ay_min = sound.mixed;
                if(sound.mixed > ay_max) ay_max = sound.mixed;
                if(ay_samples == 48000) begin
                    if(ay_commands < 300 || ay_max-ay_min < 32 || sound.psg.registers[7] == 63)
                        $fatal(1,"AY music inactive: writes=%0d range=%0d..%0d",ay_commands,ay_min,ay_max);
                    $fclose(audio_file); $fclose(ay_trace);
                    $display("PASS native VM1 system disk AY music: writes=%0d samples=%0d range=%0d..%0d",
                             ay_commands,ay_samples,ay_min,ay_max);
                    $finish;
                end
            end else audio_divider = audio_divider + 1;
        end
    end
    reg cold_complete = 0, warm_started = 0, corrupt_sram = 0, corrupted = 0;
    reg [15:0] expected_vram[0:8191];
    reg [1023:0] screen_file;
    reg [1023:0] preload_file = "build/model-preloaded.hex";
    reg check_screen = 0, screen_equal;
    reg disk_os = 0;
    integer expected_scroll = 728;
    integer expected_disk_base = 40960, expected_second_base = 0;
    integer expected_present = 1, expected_readonly = 1;
    reg [1023:0] os_prefix = "build/disk-os";
    reg keyboard_test = 0;
    reg [7:0] command[0:9];
    integer keyboard_index = 0;
    integer screen_checks = 0, screen_word;
    integer screen_base = 8192;
    integer expected_error = 0, ignored, reads_at_cold = 0;
    bk_cpu_clock native_clock(clk48,power_on,runtime_mode>=0?runtime_mode[2:0]:(model>=3?3'd1:3'd0),
        !cycle&&!strobe&&!cpu_busy,native_cpu_clk,native_cpu_n,native_frequency,cpu_stall);
    bk_cpu cpu (
        .clk_p(cpu_clk), .clk_n(!cpu_clk), .reset(cpu_reset),
        .virq(irq_sync[2]), .stop(physical_stop_test ? physical_stop_sync[1] && locked : stop_pressed),
        .irq_vector(vector), .external_input(16'o177777),
        .system_input({!locked ? 8'o200 : model == 0 ? 8'o351 : model >= 3 ? 8'o300 : 8'o200,
            {sys_input[7],physical_stop_test ? !physical_held_sync[1] : !bus.key_pending,sys_input[5:0]}}), .irq_ack(irq_ack), .peripheral_reset(peripheral_reset),
        .address(cpu_address), .write_data(cpu_data), .lanes(cpu_lanes),
        .cycle(cycle), .strobe(strobe), .write(cpu_write), .read_data(cpu_read), .acknowledge(cpu_ack),
        .system_write(sys_write), .system_read(sys_read), .external_write(ext_write)
    );
    bk_wb_cdc bridge (
        .cpu_clk(cpu_clk), .sys_clk(sys_clk), .reset(machine_reset),
        .cpu_cycle(cycle), .cpu_strobe(strobe), .cpu_write(cpu_write),
        .cpu_address(cpu_address), .cpu_data(cpu_data), .cpu_lanes(cpu_lanes),
        .cpu_ack(cpu_ack), .cpu_read(cpu_read), .cpu_busy(cpu_busy),
        .request(request), .write(write), .address(address), .write_data(write_data),
        .lanes(lanes), .ready(ready), .read_data(read_data),
        .cpu_map_register(cpu_bank), .map_register(external_bank)
    );
    bk_boot_rom rom(sys_clk, address[12:1], boot_data);
    bk_system_bus #(.CLOCK_HZ(29411764), .BIOS_CLOCK_HZ(32000)) bus (
        .clk(sys_clk), .power_on(power_on), .reset(machine_reset), .peripheral_reset(init_sync[1]),.quiesce(quiesce),.storage_idle(storage_idle),.cpu_stall(cpu_stall),
        .request(request), .write(write), .address(address), .write_data(write_data),
        .lanes(lanes), .ready(ready), .read_data(read_data),
        .mem_request(mem_request), .mem_write(mem_write), .mem_address(mem_address),
        .mem_data(mem_data), .mem_lanes(mem_lanes), .mem_ready(mem_ready), .mem_read(mem_read),
        .boot_data(boot_data), .key_valid(physical_stop_test && locked ? physical_valid : key_valid),
        .key_code(physical_stop_test && locked ? physical_code : key_code), .key_ready(key_ready),
        .keyboard_irq(irq), .keyboard_vector(vector), .irq_ack(ack_sync[1] != ack_seen),
        .scroll(scroll), .locked(locked), .boot_error(error), .boot_stage(stage),
        .sd_cs(sd_cs), .sd_sck(sd_sck), .sd_mosi(sd_mosi), .sd_miso(sd_miso),
        .map_register(external_bank), .model(model), .boot_frequency(), .color(), .video_register()
    );
    sd_spi_model #(.HIGH_CAPACITY(HIGH_CAPACITY), .VERSION2(VERSION2)) card(sd_cs,sd_sck,sd_mosi,sd_miso,reads);
    always @(posedge sys_clk) begin
        seen_locked <= locked;
        boot_reset <= locked && !seen_locked;
        // BIOS scratch RAM belongs to the operating system after ROM lock.
        if(!locked && bios_action != 0 && bios_error == 0 && ram[16'o1544 >> 1] != 0) begin
            $writememh("build/bios-error-memory.hex",ram);
            $fatal(1,"BIOS error=%02h cursor=%0d SD writes=%0d CRC=%04h/%04h CMD=%02h arg=%08h sector=%04h%04h part=%04h%04h size=%04h%04h",
                   ram[16'o1544 >> 1],ram[16'o1546 >> 1],card.writes,card.write_crc,card.received_crc,
                   card.command[0],card.argument,ram[16'o1036 >> 1],ram[16'o1034 >> 1],
                   ram[16'o1006 >> 1],ram[16'o1004 >> 1],ram[16'o1012 >> 1],ram[16'o1010 >> 1]);
        end
        if(bios_action == 0 && !entered && stage == 8'h10) begin key_valid <= 1; entered <= 1; end
        else if(key_valid && key_ready) key_valid <= 0;
        if(mem_request && !mem_ready) begin
            if(mem_address >= 131072) $fatal(1, "SRAM address overflow");
            mem_read <= ram[mem_address[16:0]]; mem_ready <= 1;
            if(mem_write) begin
                if(mem_lanes[0]) ram[mem_address[16:0]][7:0] <= mem_data[7:0];
                if(mem_lanes[1]) ram[mem_address[16:0]][15:8] <= mem_data[15:8];
                if(mem_address >= 8192 && mem_address < 16384) writes <= writes + 1;
            end
        end else if(!mem_request) mem_ready <= 0;
        if(!ay_music && !check_screen && !reset && ready && request && !write && (model >= 3 ? address == 16'o177660 : address == 16'o105) && locked && writes > 1000) begin
            polls <= polls + 1;
            if(polls == 30) begin
                if(!cold_complete) begin
                    if(model != expected_model) $fatal(1,"Wrong ROM model");
                    if(expected_error != 0) $fatal(1, "Damaged ROM was executed");
                    if(!$test$plusargs("no-dump")) $writememh("build/sd-boot-memory.hex",ram);
                    $display("PASS native SD cold boot: SDHC=%0d VERSION2=%0d sectors=%0d cycles=%0d",card.high_capacity,card.version2,reads,cycles);
                    cold_complete <= 1; reads_at_cold <= reads;
                end else if(warm_started) begin
                    if(reads != reads_at_cold || !locked) $fatal(1, "Warm reset reloaded or unlocked ROM");
                    $display("PASS warm reset: ROM lock retained, no SD reads");
                    $finish;
                end
            end
        end
        if(corrupt_sram && stage == 6 && !corrupted) begin
            ram[65536] <= ram[65536] ^ 16'h0001; corrupted <= 1;
        end
        if(error) begin
            if(stage == expected_error[7:0] && !locked) begin
                $display("PASS rejected SD/SRAM image: error=%02h ROM remains unlocked", stage); $finish;
            end
            else $fatal(1, "Bootstrap error stage=%02h pc/address=%06o ir=%06o", stage, cpu_address, cpu.core.ir);
        end
    end
    always @(posedge sys_clk) begin
        if(check_screen && locked) begin
            screen_checks <= screen_checks + 1;
            if(screen_checks == 1000000) begin
                screen_checks <= 0; screen_equal = 1;
                screen_base = model >= 3 ? (bus.video_register[15] ? 49152 : 40960) : 8192;
                for(screen_word=0;screen_word<8192;screen_word=screen_word+1)
                    // The C VM1 reference has an untimed timer; only A16M's
                    // measured-frequency text differs. Its timer self-test,
                    // model detection, memory test and disk prompt must match.
                    if(!(model == 0 && !disk_os && screen_word >= 156*32 && screen_word < 166*32) &&
                       ram[screen_base+screen_word] != expected_vram[screen_word]) screen_equal = 0;
                if(disk_os && scroll != expected_scroll[15:0]) screen_equal = 0;
                if(screen_equal && !cold_complete) begin
                    if(model != expected_model || expected_error != 0) $fatal(1,"Bad model boot");
                    if(model == 0 && ram[17'o500 >> 1] != 16'o60)
                        $fatal(1,"A16M misdetected the read-only BK10 keyboard port");
                    if(!$test$plusargs("no-dump")) $writememh("build/sd-boot-memory.hex",ram);
                    $display("PASS native model %0d boot: VRAM matches C reference%s, cycles=%0d",model,
                             model == 0 ? " except timer frequency text" : "",cycles);
                    cold_complete <= 1; reads_at_cold <= reads;
                end else if(screen_equal && warm_started) begin
                    if(reads != reads_at_cold || !locked) $fatal(1,"Warm boot changed ROM/SD");
                    $display("PASS native model %0d warm reset",model); $finish;
                end
            end
        end
    end
    reg [7:0] previous_stage = 0;
    integer locked_cycles = 0;
    integer diagnostic_cycles = 30000000;
    integer runtime_polls = 0, runtime_writes = 0;
    integer trace_count = 0;
    always @(posedge sys_clk) begin
        if(locked) locked_cycles <= locked_cycles + 1;
        if((disk_os || ay_music) && locked && locked_cycles % 20000000 == 0) begin
            $display("OS progress cycles=%0d CPU mode=%0d address=%06o bank=%06o video=%06o SD reads=%0d writes=%0d error=%0d",
                     locked_cycles,native_frequency,cpu_address,external_bank,bus.video_register,reads,card.writes,bus.fdd_error);
`ifndef BK_VM1_VENDOR_RAM
            $display("OS CPU PC=%06o PSW=%06o",cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw);
`endif
            if($test$plusargs("os-progress-dump"))
                $writememh($sformatf("%0s-progress.hex",os_prefix),ram);
            $fflush();
        end
        if(locked && request && ready) begin
            if(stop_started && stop_trace < 300) begin
                $display("STOP BUS %s %06o %06o bank=%06o PC=%06o PSW=%06o stop=%b",write ? "W" : "R",
                    address,write ? write_data : read_data,external_bank,
                    cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw,physical_stop_sync[1]);
                stop_trace <= stop_trace + 1;
            end
            if(($test$plusargs("trace-runtime") || ($test$plusargs("trace-warm") && warm_started)) && trace_count < 5000) begin
                $display("BUS %s %06o %06o bank=%06o",write ? "W" : "R",address,write ? write_data : read_data,external_bank);
                trace_count <= trace_count + 1;
            end
            if(address == 16'o177660) runtime_polls <= runtime_polls + 1;
            if(write && address >= 16'o40000 && address < 16'o100000) runtime_writes <= runtime_writes + 1;
        end
        if($test$plusargs("diagnostic") && locked_cycles == diagnostic_cycles) begin
            $display("DIAGNOSTIC model=%0d bank=%06o video=%06o scroll=%06o writes=%0d polls=%0d ir=%06o adr=%06o",
                     model,cpu_bank,bus.video_register,scroll,runtime_writes,runtime_polls,cpu.core.ir,cpu_address);
`ifndef BK_VM1_VENDOR_RAM
            $display("REG psw=%06o R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PC=%06o",cpu.core.psw,cpu.core.vreg_rm.vm1_vcram_reg.ram[15],cpu.core.vreg_rm.vm1_vcram_reg.ram[7],cpu.core.vreg_rm.vm1_vcram_reg.ram[11],cpu.core.vreg_rm.vm1_vcram_reg.ram[3],cpu.core.vreg_rm.vm1_vcram_reg.ram[13],cpu.core.vreg_rm.vm1_vcram_reg.ram[5],cpu.core.vreg_rm.vm1_vcram_reg.ram[9],cpu.core.vreg_rm.vm1_vcram_reg.ram[1]);
`endif
            $writememh("build/sd-diagnostic.hex",ram);
            $finish;
        end
    end
    always @(posedge cpu_clk or posedge cpu_reset) begin
        cycles <= cycles + 1;
        if(cpu_reset) begin sys_input <= 8'o370; sys_output <= 8'o220; cpu_bank <= 0; end
        else if(sys_write) begin
            sys_input[2] <= 1;
            if(model >= 3 && cpu_lanes == 3 && cpu_data[11]) cpu_bank <= cpu_data;
            else if(cpu_lanes[0] && !(model >= 3 && cpu_data[11])) sys_output <= cpu_data[7:0] & 8'o360;
        end
        else if(sys_read) sys_input[2] <= 0;
        if(stage != previous_stage) begin
            $display("BOOT stage=%02h cycle=%0d address=%06o reads=%0d",stage,cycles,cpu_address,reads);
            previous_stage <= stage; $fflush();
        end
    end
    task bios_key(input [7:0] code);
        begin
            wait(key_ready && !key_valid);
            @(negedge sys_clk); key_code = code; key_valid = 1;
            @(negedge sys_clk); key_valid = 0;
            wait(!key_ready); wait(key_ready);
        end
    endtask
    initial begin
        native_rate=$test$plusargs("native-rate");
        physical_stop_test=$test$plusargs("physical-stop");
        ay_music=$test$plusargs("ay-music");
        if(ay_music) begin
            audio_file=$fopen("build/ay-music-pcm.txt","w");
            ay_trace=$fopen("build/ay-music-registers.txt","w");
        end
        disk_os=$test$plusargs("disk-os");
        ignored=$value$plusargs("runtime-mode=%d",runtime_mode);
        ignored=$value$plusargs("expected-scroll=%d",expected_scroll);
        ignored=$value$plusargs("disk-base=%d",expected_disk_base);
        ignored=$value$plusargs("second-base=%d",expected_second_base);
        ignored=$value$plusargs("disk-present=%d",expected_present);
        ignored=$value$plusargs("disk-readonly=%d",expected_readonly);
        ignored=$value$plusargs("os-prefix=%s",os_prefix);
        ignored = $value$plusargs("expected-model=%d", expected_model);
        check_screen = $value$plusargs("screen=%s",screen_file);
        ignored = $value$plusargs("preload=%s",preload_file);
        if(check_screen) $readmemh(screen_file,expected_vram);
        ignored = $value$plusargs("diagnostic-cycles=%d", diagnostic_cycles);
        ignored = $value$plusargs("bios-action=%d", bios_action);
        ignored = $value$plusargs("bios-error=%h", bios_error);
        ignored = $value$plusargs("error=%h", expected_error);
        corrupt_sram = $test$plusargs("corrupt-sram");
        for(i=0;i<131072;i=i+1) ram[i] = 0;
        if($test$plusargs("preloaded")) begin
            $readmemh(preload_file,ram);
            #100; power_on = 0;
            wait(!bus.disk_partitions.clearing);
            // This fixture bypasses BIOS, so adopt its sealed empty drive
            // configuration as well as the preloaded ROM. The runtime host
            // must reach idle before the production-style warm-reset drain.
            @(negedge sys_clk); bus.disk_partitions.control = 16'h8001;
            bus.model = expected_model; bus.locked = 1; bus.boot_stage = 8'hff;
            #100; reset = 0;
        end else begin #200; reset = 0; power_on = 0; end
        if(bios_action != 0) begin
            wait(stage == 8'h10 || stage == 8'h11);
            if(stage == 8'h10) bios_key(8'h18); // Backspace opens the setup menu.
            wait(stage == 8'h11);
            if(bios_action == 5) begin
                repeat(3) bios_key(8'h14); // A: 6 RO -> 5 RO -> 3 RO, skipping extended PART 4.
                repeat(4) bios_key(8'h11);
                repeat(4) bios_key(8'h14); // Save row.
            end else if(bios_action == 4) begin
                repeat(3) bios_key(8'h14); // A: PART 6 RO -> PART 5 RO.
                repeat(2) bios_key(8'h11);
                bios_key(8'h14); // B: PART 5 RO -> PART 6 RO.
                repeat(2) bios_key(8'h12);
                repeat(4) bios_key(8'h14); // Boot without save.
            end else if(bios_action == 3) begin
                bios_key(8'h12); // Model 2 -> 3, stock 4 MHz and drives A/B.
                repeat(2) bios_key(8'h14); // Video row.
                bios_key(8'h12); // MONO -> COLOR.
                repeat(5) bios_key(8'h14); // Save row.
            end else begin
                bios_key(8'h14); // Down: frequency row.
                repeat(5) bios_key(8'h12); // 3 -> 4 -> 6 -> 8 -> 12 -> 16 MHz.
                repeat(bios_action == 2 ? 7 : 6) bios_key(8'h14); // Boot or Save row.
            end
            bios_key(8'h0a);
            if(bios_error != 0) begin
                wait(ram[16'o1544 >> 1] == bios_error);
                if(locked || error) $fatal(1,"BIOS save failure locked ROM");
                $display("PASS native BIOS refused save: error=%02h SD writes=%0d",bios_error,card.writes);
                $finish;
            end
        end
        keyboard_test = $test$plusargs("keyboard");
        command[0]="P"; command[1]="R"; command[2]="I"; command[3]="N"; command[4]="T";
        command[5]=" "; command[6]="2"; command[7]="+"; command[8]="2"; command[9]=8'o12;
        wait(cold_complete);
        if(disk_os) begin
            if(model != expected_model || bus.disk_present != expected_present || bus.disk_readonly != expected_readonly ||
               bus.disk_partitions.bases[0] != expected_disk_base ||
               (expected_second_base != 0 && bus.disk_partitions.bases[1] != expected_second_base) ||
               card.writes != 0 || reads < 200 || bus.fdd_error)
                $fatal(1,"Disk OS did not boot from the protected raw partition");
            $writememh($sformatf("%0s-memory.hex",os_prefix),ram);
            ignored=$fopen($sformatf("%0s-registers.json",os_prefix),"w");
            $fdisplay(ignored,"{\"model\":%0d,\"frequency_index\":%0d,\"video\":%0d,\"scroll\":%0d,\"reads\":%0d,\"writes\":%0d}",
                      model,native_frequency,bus.video_register,scroll,reads,card.writes);
            $fclose(ignored);
            $display("PASS disk OS cold boot: native CPU/BIOS/ROM/KNGMD/raw partition/SPI; VRAM matches C; reads=%0d writes=%0d page=%0d scroll=%06o",reads,card.writes,bus.video_register[15]?6:5,scroll);
            $finish;
        end
        if(!disk_os) begin
        if(keyboard_test) begin
            for(keyboard_index=0;keyboard_index<10;keyboard_index=keyboard_index+1) begin
                repeat(1000000) @(posedge cpu_clk);
                bios_key(command[keyboard_index]);
            end
            repeat(3000000) @(posedge cpu_clk);
            $writememh("build/model-print-memory.hex",ram);
            $display("PASS native model %0d received PRINT 2+2",model); $finish;
        end
        if(bios_action != 0) begin
            if(bios_action == 3 || bios_action == 5) begin
                if(bus.boot_frequency != 1 || bus.model != 3 || bus.color != (bios_action == 3) || bus.disk_present != 3 ||
                   bus.disk_partitions.bases[0] != ($test$plusargs("logical-bios") ? expected_disk_base : 34816) ||
                   bus.disk_partitions.bases[1] != ($test$plusargs("logical-bios") ? expected_second_base : 36864) ||
                   ($test$plusargs("logical-bios") && bus.disk_readonly != 3))
                    $fatal(1,"BIOS model/video/raw partitions were not applied");
            end else if(bus.boot_frequency != 5 || bus.model != 2) $fatal(1,"BIOS edits were not applied");
            if(bios_action == 2 && card.writes != 0) $fatal(1,"Boot without save wrote SD");
            if(bios_action == 1 && card.writes == 0) $fatal(1,"Save did not write SD");
            $display("PASS native BIOS %s: model=%0d frequency index=%0d color=%0d raw drives=%h SD writes=%0d",
                     bios_action == 2 ? "temporary" : "save",bus.model,bus.boot_frequency,bus.color,bus.disk_present,card.writes);
        end
        #1000; quiesce = 1;
        wait(!cycle && !strobe && !cpu_busy && storage_idle);
        @(negedge sys_clk); reset = 1;
        #500; reset = 0; polls = 0; warm_started = 1; quiesce = 0;
        $display("WARM model=%0d cpu_bank=%06o SD reads=%0d",model,cpu_bank,reads);
        $fflush();
        end
    end
    // BIOS is accelerated; runtime disk profiles must use a supported CPU
    // rate. Switch only while both candidate clocks are low during handoff.
    initial if($test$plusargs("native-runtime")) begin
        wait(locked); wait(!fast_cpu_clk && !native_cpu_clk);
        native_rate = 1;
        $display("PASS switched native runtime to model %0d frequency index=%0d",model,native_frequency);
        $fflush();
    end
    initial if($test$plusargs("disk-stop")) begin
        wait(locked && locked_cycles >= 200000000);
        @(negedge cpu_clk); stop_pressed = 1;
        repeat(2000) @(negedge cpu_clk);
        stop_pressed = 0;
        $display("Pressed/released STOP to exit the system disk's music intro");
    end
    task physical_bit(input value);
        begin ps2_data = value; #15000; ps2_low = 1; #30000; ps2_low = 0; #15000; end
    endtask
    task physical_send(input [7:0] value);
        integer bit_number;
        begin
            wait(ps2_clock); #10000; physical_bit(0);
            for(bit_number=0;bit_number<8;bit_number=bit_number+1) physical_bit(value[bit_number]);
            physical_bit(~^value); physical_bit(1); ps2_data = 1; #50000;
        end
    endtask
    initial if($test$plusargs("physical-stop")) begin
        wait(player_writes >= 100);
        $display("STOP test: running AY player writes=%0d PC=%06o PSW=%06o",player_writes,
            cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw); $fflush();
        physical_send(8'h1c); physical_send(8'hf0); physical_send(8'h1c);
        physical_send(8'h32); physical_send(8'hf0); physical_send(8'h32);
        stop_started = 1;
        physical_send(8'h76);
        if(!physical_stop) $fatal(1,"Physical Esc did not reach STOP decoder");
        #120000000;
        physical_send(8'hf0); physical_send(8'h76);
        if(physical_stop) $fatal(1,"Physical Esc release did not clear STOP decoder");
        $display("STOP test: released physical Esc after 120 ms; reads=%0d PC=%06o PSW=%06o",reads,
            cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw); $fflush();
        if($test$plusargs("stop-debug")) begin
            #400000000;
            $display("STOP diagnostic: reads=%0d writes=%0d PC=%06o PSW=%06o bank=%06o video=%06o ay_writes=%0d",
                reads,card.writes,cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw,cpu_bank,bus.video_register,player_writes);
            $writememh("build/stop-diagnostic-memory.hex",ram);
            $finish;
        end
    end
    initial begin
        if($test$plusargs("disk-os") || $test$plusargs("ay-music")) #120000000000; else #12000000000;
        $writememh("build/sd-timeout.hex",ram);
        $fatal(1, "SD boot timeout stage=%02h reads=%0d adr=%06o ir=%06o",stage,reads,cpu_address,cpu.core.ir);
    end
endmodule
