`timescale 1ns/1ps
// Genuine CPU-executed cold boot: only the resident EBR has initial contents.
module tb_sd_boot;
    parameter HIGH_CAPACITY = 1, VERSION2 = 1;
    reg cpu_clk = 0, sys_clk = 0, reset = 1, power_on = 1;
    always #10 cpu_clk = !cpu_clk;
    always #17 sys_clk = !sys_clk;
    wire [15:0] cpu_address, cpu_data, cpu_read, address, write_data, read_data;
    wire [1:0] cpu_lanes, lanes;
    wire cycle, strobe, cpu_write, cpu_ack, cpu_busy, request, write, ready;
    wire irq, irq_ack, peripheral_reset, sys_write, sys_read;
    wire [15:0] vector, scroll;
    wire locked, error;
    wire [7:0] stage;
    wire mem_request, mem_write;
    wire [19:0] mem_address;
    wire [15:0] mem_data, boot_data;
    wire [1:0] mem_lanes;
    reg mem_ready = 0;
    reg [15:0] mem_read = 0;
    reg [15:0] ram[0:32767];
    wire sd_cs, sd_sck, sd_mosi, sd_miso;
    integer reads;
    integer cycles = 0, writes = 0, polls = 0, i;
    reg [7:0] sys_input = 8'o370;
    reg cold_complete = 0, warm_started = 0, corrupt_sram = 0, corrupted = 0;
    integer expected_error = 0, ignored, reads_at_cold = 0;
    bk_cpu cpu (
        .clk_p(cpu_clk), .clk_n(!cpu_clk), .reset(reset),
        .virq(irq), .stop(1'b0), .irq_vector(vector), .external_input(16'o177777),
        .system_input({8'o200,sys_input}), .irq_ack(irq_ack), .peripheral_reset(peripheral_reset),
        .address(cpu_address), .write_data(cpu_data), .lanes(cpu_lanes),
        .cycle(cycle), .strobe(strobe), .write(cpu_write), .read_data(cpu_read), .acknowledge(cpu_ack),
        .system_write(sys_write), .system_read(sys_read), .external_write()
    );
    bk_wb_cdc bridge (
        .cpu_clk(cpu_clk), .sys_clk(sys_clk), .reset(reset),
        .cpu_cycle(cycle), .cpu_strobe(strobe), .cpu_write(cpu_write),
        .cpu_address(cpu_address), .cpu_data(cpu_data), .cpu_lanes(cpu_lanes),
        .cpu_ack(cpu_ack), .cpu_read(cpu_read), .cpu_busy(cpu_busy),
        .request(request), .write(write), .address(address), .write_data(write_data),
        .lanes(lanes), .ready(ready), .read_data(read_data)
    );
    bk_boot_rom rom(sys_clk, address[12:1], boot_data);
    bk_system_bus bus (
        .clk(sys_clk), .power_on(power_on), .reset(reset), .peripheral_reset(1'b0),
        .request(request), .write(write), .address(address), .write_data(write_data),
        .lanes(lanes), .ready(ready), .read_data(read_data),
        .mem_request(mem_request), .mem_write(mem_write), .mem_address(mem_address),
        .mem_data(mem_data), .mem_lanes(mem_lanes), .mem_ready(mem_ready), .mem_read(mem_read),
        .boot_data(boot_data), .key_valid(1'b0), .key_code(8'b0), .key_ready(),
        .keyboard_irq(irq), .keyboard_vector(vector), .irq_ack(irq_ack),
        .scroll(scroll), .locked(locked), .boot_error(error), .boot_stage(stage),
        .sd_cs(sd_cs), .sd_sck(sd_sck), .sd_mosi(sd_mosi), .sd_miso(sd_miso)
    );
    sd_spi_model #(.HIGH_CAPACITY(HIGH_CAPACITY), .VERSION2(VERSION2)) card(sd_cs,sd_sck,sd_mosi,sd_miso,reads);
    always @(posedge sys_clk) begin
        if(mem_request && !mem_ready) begin
            if(mem_address >= 32768) $fatal(1, "SRAM address overflow");
            mem_read <= ram[mem_address[14:0]]; mem_ready <= 1;
            if(mem_write) begin
                if(mem_lanes[0]) ram[mem_address[14:0]][7:0] <= mem_data[7:0];
                if(mem_lanes[1]) ram[mem_address[14:0]][15:8] <= mem_data[15:8];
                if(mem_address >= 8192 && mem_address < 16384) writes <= writes + 1;
            end
        end else if(!mem_request) mem_ready <= 0;
        if(!reset && ready && request && !write && address == 16'o105 && locked && writes > 1000) begin
            polls <= polls + 1;
            if(polls == 30) begin
                if(!cold_complete) begin
                    if(expected_error != 0) $fatal(1, "Damaged ROM was executed");
                    $writememh("build/sd-boot-memory.hex",ram);
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
            ram[16384] <= ram[16384] ^ 16'h0001; corrupted <= 1;
        end
        if(error) begin
            if(stage == expected_error[7:0] && !locked) begin
                $display("PASS rejected SD/SRAM image: error=%02h ROM remains unlocked", stage); $finish;
            end
            else $fatal(1, "Bootstrap error stage=%02h pc/address=%06o ir=%06o", stage, cpu_address, cpu.core.ir);
        end
    end
    reg [7:0] previous_stage = 0;
    always @(posedge cpu_clk) begin
        cycles <= cycles + 1;
        if(reset) sys_input <= 8'o370;
        else if(sys_write) sys_input[2] <= 1;
        else if(sys_read) sys_input[2] <= 0;
        if(stage != previous_stage) begin
            $display("BOOT stage=%02h cycle=%0d address=%06o reads=%0d",stage,cycles,cpu_address,reads);
            previous_stage <= stage;
        end
    end
    initial begin
        ignored = $value$plusargs("error=%h", expected_error);
        corrupt_sram = $test$plusargs("corrupt-sram");
        for(i=0;i<32768;i=i+1) ram[i] = 0;
        #200; reset = 0; power_on = 0;
        wait(cold_complete);
        #1000; reset = 1;
        #500; reset = 0; polls = 0; warm_started = 1;
    end
    initial begin #3000000000; $fatal(1, "SD boot timeout stage=%02h reads=%0d adr=%06o ir=%06o",stage,reads,cpu_address,cpu.core.ir); end
endmodule
