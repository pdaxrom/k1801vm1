`timescale 1ns/1ps
// System-domain native CSR, SPI and retained ROM bootstrap ports.
module bk_system_bus #(parameter integer CLOCK_HZ = 24000000,
                       parameter integer BIOS_CLOCK_HZ = CLOCK_HZ) (
    input wire clk, power_on, reset, peripheral_reset, quiesce,
    output wire storage_idle, cpu_stall,
    input wire request, write,
    input wire [15:0] address, write_data,
    input wire [1:0] lanes,
    output reg ready = 0,
    output reg [15:0] read_data = 0,
    output wire mem_request, mem_write,
    output reg [19:0] mem_address = 0,
    output reg [15:0] mem_data = 0,
    output reg [1:0] mem_lanes = 0,
    input wire mem_ready,
    input wire [15:0] mem_read,
    input wire [15:0] boot_data,
    input wire key_valid,
    input wire [7:0] key_code,
    output wire key_ready,
    output wire keyboard_irq,
    output wire [15:0] keyboard_vector,
    input wire irq_ack,
    output reg [15:0] scroll = 16'o001330,
    output reg locked = 0,
    output reg boot_error = 0,
    output reg [7:0] boot_stage = 0,
    output reg sd_cs = 1 /* synthesis syn_useioff=1 */,
    output wire sd_sck, sd_mosi,
    input wire sd_miso,
    input wire [15:0] map_register,
    output reg [2:0] model = 2, boot_frequency = 0,
    output reg color = 0,
    output reg [15:0] video_register = 16'o040000
);
    localparam IDLE = 0, MEMORY = 1, ROM = 2, COMPLETE = 3, UNMAPPED = 4, FDD = 5, CRC_BITS = 6;
    reg [2:0] state = IDLE;
    reg memory_write = 0;
    reg [1:0] boot_operation = 0;
    reg boot_odd = 0;
    reg [15:0] pointer = 16'h8000;
    reg [16:0] written_count = 0, verified_count = 0, expected_size = 32768;
    reg [3:0] pointer_high = 0;
    reg physical_pointer = 0;
    reg [6:0] a16_mode = 7'o160;
    reg [1:0] a16_phase = 0;
    reg tick_pending = 0, frame_pending = 0;
    wire tick50, frame50;
    reg disk_config_write = 0;
    reg [1:0] disk_config_address = 0;
    reg [15:0] disk_config_data = 0;
    wire [15:0] disk_config_read;
    wire [3:0] disk_present, disk_readonly;
    wire disk_block_mode, disk_configured, disk_allowed;
    wire [31:0] disk_lba;
    wire [1:0] disk_sector_drive;
    wire [10:0] disk_sector_offset;
    wire disk_sector_request, disk_sector_write, disk_sector_ready, disk_sector_done, disk_sector_error;
    wire fdd_ready,fdd_idle,fdd_error;
    wire [15:0] fdd_read;
    assign cpu_stall = state == FDD && !fdd_ready;
    wire [8:0] disk_buffer_address;
    wire disk_buffer_write;
    wire [7:0] disk_buffer_data,disk_buffer_read;
    wire runtime_cs,runtime_start;
    wire [7:0] runtime_tx,runtime_divider;
    wire boot_cs;
    // Register the ownership mux before the pin. Both owners select CS before
    // starting their byte engine; the extra system clock preserves that setup.
    always @(posedge clk)
        if(power_on) sd_cs <= 1; else sd_cs <= locked ? runtime_cs : boot_cs;
    assign storage_idle = fdd_idle && !spi_busy && !byte_busy && sd_cs &&
                          (!locked || disk_sector_ready) && !disk_sector_request;
    bk_fdd #(.CLOCK_HZ(CLOCK_HZ)) disk_controller (
        .clk(clk),.power_on(power_on),.peripheral_reset(reset||peripheral_reset),.quiesce(quiesce),
        .request(state==FDD),.write(write),.data_port(word_address[1]),
        .write_data(model==0&&!word_address[1] ? write_data & 16'hfff3 : write_data),
        .ready(fdd_ready),.read_data(fdd_read),.idle(fdd_idle),.error(fdd_error),
        .present(disk_present),.readonly(disk_readonly),
        .sector_request(disk_sector_request),.sector_write(disk_sector_write),
        .sector_drive(disk_sector_drive),.sector_offset(disk_sector_offset),
        .sector_ready(disk_sector_ready),.sector_done(disk_sector_done),.sector_error(disk_sector_error),
        .buffer_address(disk_buffer_address),.buffer_write(disk_buffer_write),
        .buffer_data(disk_buffer_data),.buffer_read(disk_buffer_read)
    );
    wire sd_host_request=disk_sector_request&&disk_allowed;
    wire sd_host_ready,sd_host_done,sd_host_error;
    assign disk_sector_ready = sd_host_ready;
    assign disk_sector_done = sd_host_done || (disk_sector_request&&!disk_allowed);
    assign disk_sector_error = sd_host_error || !disk_allowed;
    bk_sd_host disk_host (
        .clk(clk),.reset(power_on),.request(sd_host_request),.write(disk_sector_write),.lba(disk_lba),
        .ready(sd_host_ready),.initialized(),.done(sd_host_done),.error(sd_host_error),
        .buffer_address(disk_buffer_address),.buffer_write(disk_buffer_write),
        .buffer_data_in(disk_buffer_data),.buffer_data_out(disk_buffer_read),.sd_cs(runtime_cs),
        .init_valid(locked&&disk_configured),.block_mode(disk_block_mode),
        .byte_start(runtime_start),.byte_tx(runtime_tx),.divider(runtime_divider),
        .byte_busy(byte_busy),.byte_done(byte_done),.byte_rx(byte_rx)
    );
    bk_sd_partitions disk_partitions (
        .clk(clk), .power_on(power_on), .enabled(!locked),
        .config_write(disk_config_write), .config_address(disk_config_address),
        .config_data(disk_config_data), .config_read(disk_config_read),
        .drive(disk_sector_drive), .offset(disk_sector_offset), .writing(disk_sector_write), .allowed(disk_allowed), .lba(disk_lba),
        .present(disk_present), .readonly(disk_readonly), .block_mode(disk_block_mode), .configured(disk_configured)
    );
    uj11_tick #(.DIVISOR(BIOS_CLOCK_HZ / 50)) bios_timebase(clk,power_on,tick50);
    generate if(BIOS_CLOCK_HZ == CLOCK_HZ) begin
        assign frame50 = tick50;
    end else begin
        uj11_tick #(.DIVISOR(CLOCK_HZ / 50)) frame_timebase(clk,power_on,frame50);
    end endgenerate
    wire mapped;
    wire [19:0] mapped_address;
    bk_memory_map mapper(model,map_register,a16_mode,address,write,mapped,mapped_address);
    wire [19:0] stream_address = physical_pointer ? {pointer_high,pointer} :
                                {4'b0010,1'b0,pointer[14:0]};
    reg [31:0] crc = 32'hffffffff, expected_crc = 0;
    reg key_pending = 0, irq_pending = 0, irq_disabled = 0;
    reg irq_granted = 0;
    reg [15:0] granted_vector = 16'o60;
    reg [7:0] key_latch = 0;
    reg spi_write = 0;
    reg [2:0] spi_address = 0;
    reg [7:0] spi_data = 0;
    wire [7:0] spi_result, byte_data, byte_divider, byte_rx;
    wire spi_busy, byte_busy, byte_done, byte_start;
    wire [15:0] word_address = {address[15:1], 1'b0};
    assign key_ready = !key_pending;
    assign keyboard_irq = irq_granted;
    assign keyboard_vector = granted_vector;
    assign mem_request = state == MEMORY;
    assign mem_write = memory_write;
    boot_spi_registers spi_regs (
        .clk(clk), .reset(power_on || reset), .enabled(!locked),
        .bus_write(spi_write), .address(spi_address), .data(spi_data),
        .result(spi_result), .busy(spi_busy), .cs(boot_cs),
        .byte_busy(byte_busy), .byte_done(byte_done), .byte_rx(byte_rx),
        .byte_start(byte_start), .byte_data(byte_data), .byte_divider(byte_divider)
    );
    spi_byte_master shifter (
        .clk(clk), .reset(power_on || reset), .start(locked ? runtime_start : byte_start),
        .tx_data(locked ? runtime_tx : byte_data), .divider(locked ? runtime_divider : byte_divider), .miso(sd_miso),
        .sck(sd_sck), .mosi(sd_mosi), .busy(byte_busy), .done(byte_done), .rx_data(byte_rx)
    );
    // SPI address is stable while the upstream bridge holds its transfer.
    always @* begin
        case (word_address)
            16'o177600: spi_address = 1;
            16'o177602: spi_address = 2;
            16'o177604: spi_address = 3;
            16'o177606: spi_address = 0;
            default: spi_address = 4;
        endcase
    end
    reg [3:0] crc_bits = 0;
    wire [7:0] crc_input = state == MEMORY ?
        (boot_operation == 1 ? mem_data[7:0] : boot_odd ? mem_read[15:8] : mem_read[7:0]) : write_data[7:0];

    always @(posedge clk) begin
        spi_write <= 0;
        disk_config_write <= 0;
        disk_config_address <= word_address == 16'o177652 ? 0 : word_address == 16'o177654 ? 1 : 2;
        if (power_on) begin
            locked <= 0; boot_error <= 0; boot_stage <= 0;
            pointer <= 16'h8000; written_count <= 0; verified_count <= 0;
            crc <= 32'hffffffff; expected_crc <= 0;
            model <= 2; boot_frequency <= 0; color <= 0; pointer_high <= 0; physical_pointer <= 0;
            expected_size <= 32768; tick_pending <= 0;
        end
        if (power_on || reset) begin
            state <= IDLE; ready <= 0; read_data <= 0;
            mem_address <= 0; mem_data <= 0; mem_lanes <= 0;
            memory_write <= 0; boot_operation <= 0; boot_odd <= 0; crc_bits <= 0;
            key_pending <= 0; irq_pending <= 0; irq_disabled <= locked && model >= 3; key_latch <= 0;
            scroll <= 16'o001330; spi_data <= 0; video_register <= 16'o040000;
            a16_mode <= 7'o160; a16_phase <= 0; frame_pending <= 0;
            irq_granted <= 0; granted_vector <= 16'o60;
        end else begin
            if (peripheral_reset) begin
                key_pending <= 0; irq_pending <= 0; irq_disabled <= locked && model >= 3;
                // INIT withdraws pending IRQs but preserves the BK video latches.
                // Music installs IRQ100 before RESET and keeps frame IRQ enabled.
                frame_pending <= 0; irq_granted <= 0;
            end else begin
                if (irq_ack && irq_granted) begin
                    irq_granted <= 0;
                    if(granted_vector == 16'o100) frame_pending <= 0;
                    else irq_pending <= 0;
                end else if(!irq_granted && !irq_ack) begin
                    if(irq_pending && !irq_disabled) begin
                        granted_vector <= key_latch[7] ? 16'o274 : 16'o60; irq_granted <= 1;
                    end else if(frame_pending && locked && model >= 3) begin
                        granted_vector <= 16'o100; irq_granted <= 1;
                    end
                end else if(irq_granted && !irq_ack &&
                    (granted_vector == 16'o100 ? !frame_pending : (!irq_pending || irq_disabled))) irq_granted <= 0;
                if (key_valid && key_ready) begin
                    key_latch <= key_code; key_pending <= 1; irq_pending <= 1;
                end
            end
            case (state)
                IDLE: if (request) begin
                    boot_operation <= 0;
                    if(locked && (model == 0 || model == 3) &&
                       (word_address == 16'o177130 || word_address == 16'o177132) &&
                       (write || model == 3 || !a16_mode[2])) begin
                        ready <= 0; state <= FDD;
                        if(write && model == 0 && word_address == 16'o177130 && lanes == 3) begin
                            a16_mode[3:2] <= write_data[3:2];
                            if(a16_phase != 0) begin
                                a16_mode[6:4] <= write_data[6:4];
                                a16_phase <= (write_data[2:1] == 3) ? 1 : 0;
                            end else if(write_data[2:1] == 3) a16_phase <= 1;
                        end
                    end else if ((!locked && address < 16'o100000) || (locked && mapped)) begin
                        mem_address <= locked ? {1'b0,mapped_address[19:1]} : {5'b0, address[15:1]};
                        memory_write <= write; mem_data <= write_data; mem_lanes <= lanes;
                        state <= MEMORY;
                    end else if (!locked && address >= 16'o100000 && address < 16'o120000 && !write) begin
                        state <= ROM;
                    end else if (word_address == 16'o177660) begin
                        read_data <= {8'b0, key_pending, irq_disabled, 6'b0};
                        if (write && lanes[0]) irq_disabled <= write_data[6];
                        ready <= 1; state <= COMPLETE;
                    end else if (word_address == 16'o177662 && write && (!locked || model < 3)) begin
                        // BK10's keyboard data CSR is read-only. A16M probes its
                        // writable BK11 video alias to choose the host model.
                        state <= UNMAPPED;
                    end else if (word_address == 16'o177662) begin
                        read_data <= {9'b0, key_latch[6:0]};
                        if (!write && lanes[0]) begin key_pending <= 0; irq_pending <= 0; end
                        if(write && lanes[1] && model >= 3 && locked) begin
                            video_register <= write_data & 16'hcf00;
                            if(write_data[14]) frame_pending <= 0;
                        end
                        ready <= 1; state <= COMPLETE;
                    end else if (word_address == 16'o177664) begin
                        read_data <= scroll;
                        if (write) begin
                            if (lanes[0]) scroll[7:0] <= write_data[7:0];
                            if (lanes[1]) scroll[15:8] <= write_data[15:8] & 8'h02;
                        end
                        ready <= 1; state <= COMPLETE;
                    end else if (!locked && word_address >= 16'o177600 && word_address <= 16'o177606) begin
                        read_data <= {8'b0, spi_result};
                        spi_data <= write_data[7:0];
                        if (write && lanes[0]) spi_write <= 1;
                        ready <= 1; state <= COMPLETE;
                    end else if (!locked && word_address >= 16'o177610 && word_address <= 16'o177656) begin
                        ready <= 1; state <= COMPLETE;
                        case (word_address)
                            16'o177610: begin
                                read_data <= {locked, boot_error, 6'b0, boot_stage};
                                if (write && write_data == 0) begin
                                    boot_error <= 0; written_count <= 0; verified_count <= 0;
                                    crc <= 32'hffffffff; expected_crc <= 0; pointer <= 16'h8000;
                                    pointer_high <= 0; physical_pointer <= 0; expected_size <= 32768;
                                end
                            end
                            16'o177612: begin
                                read_data <= {8'b0, boot_stage};
                                if (write && lanes[0]) begin
                                    boot_stage <= write_data[7:0];
                                    if (write_data[15]) boot_error <= 1;
                                end
                            end
                            16'o177614: begin
                                read_data <= pointer;
                                if (write) begin
                                    if (lanes[0]) pointer[7:0] <= write_data[7:0];
                                    if (lanes[1]) pointer[15:8] <= write_data[15:8];
                                end
                            end
                            16'o177616, 16'o177620: begin
                                if (stream_address < 20'h20000 || stream_address >= 20'h30000 ||
                                        (word_address == 16'o177616 && !write) ||
                                        (word_address == 16'o177620 && write)) begin
                                    boot_error <= 1;
                                end else begin
                                    mem_address <= {1'b0,stream_address[19:1]};
                                    memory_write <= word_address == 16'o177616;
                                    mem_data <= {write_data[7:0], write_data[7:0]};
                                    mem_lanes <= pointer[0] ? 2'b10 : 2'b01;
                                    boot_odd <= pointer[0];
                                    boot_operation <= word_address == 16'o177616 ? 1 : 2;
                                    ready <= 0; state <= MEMORY;
                                end
                            end
                            16'o177622: begin
                                read_data <= 0;
                                if (write && lanes[0]) begin
                                    crc <= crc ^ {24'b0,crc_input}; crc_bits <= 8;
                                    ready <= 0; state <= CRC_BITS;
                                end
                            end
                            16'o177624: begin
                                read_data <= 0;
                                if (write) begin crc <= 32'hffffffff; verified_count <= 0; end
                            end
                            16'o177626: read_data <= ~crc[15:0];
                            16'o177630: read_data <= ~crc[31:16];
                            16'o177632: begin
                                read_data <= expected_crc[15:0];
                                if (write) begin
                                    if (lanes[0]) expected_crc[7:0] <= write_data[7:0];
                                    if (lanes[1]) expected_crc[15:8] <= write_data[15:8];
                                end
                            end
                            16'o177634: begin
                                read_data <= expected_crc[31:16];
                                if (write) begin
                                    if (lanes[0]) expected_crc[23:16] <= write_data[7:0];
                                    if (lanes[1]) expected_crc[31:24] <= write_data[15:8];
                                end
                            end
                            16'o177636: begin
                                read_data <= 0;
                                if (write && write_data == 16'ha55a) begin
                                    if (!boot_error && written_count == expected_size && verified_count == expected_size &&
                                            ~crc == expected_crc) begin
                                        locked <= 1; boot_stage <= 8'hff;
                                        // The BIOS Enter/Backspace must not become a BOS startup command.
                                        key_latch <= 0; key_pending <= 0; irq_pending <= 0;
                                        irq_disabled <= model >= 3; irq_granted <= 0; frame_pending <= 0;
                                    end
                                    else boot_error <= 1;
                                end
                            end
                            16'o177640: begin
                                read_data <= {12'b0,pointer_high};
                                if(write && lanes[0] && write_data[15:4] == 0) begin
                                    pointer_high <= write_data[3:0]; physical_pointer <= 1;
                                end
                            end
                            16'o177642: begin
                                read_data <= expected_size[15:0];
                                if(write) expected_size <= write_data == 0 ? 17'd65536 : {1'b0,write_data};
                            end
                            16'o177644: begin
                                read_data <= {13'b0,model};
                                if(write && write_data <= 4) model <= write_data[2:0];
                            end
                            16'o177646: begin
                                read_data <= {12'b0,color,boot_frequency};
                                if(write && write_data[2:0] <= 6 && write_data[15:4] == 0) begin
                                    boot_frequency <= write_data[2:0]; color <= write_data[3];
                                end
                            end
                            16'o177650: begin
                                read_data <= {15'b0,tick_pending};
                                if(!write) tick_pending <= 0;
                            end
                            16'o177652,16'o177654,16'o177656: begin
                                read_data <= disk_config_read;
                                disk_config_data <= write_data;
                                if(write && lanes == 3) disk_config_write <= 1;
                            end
                            default: read_data <= 0;
                        endcase
                    end else state <= UNMAPPED;
                end
                MEMORY: if (mem_ready) begin
                    read_data <= mem_read;
                    if (boot_operation != 0) begin
                        pointer <= pointer + 1'b1;
                        if(physical_pointer && pointer == 16'hffff) pointer_high <= pointer_high + 1'b1;
                        crc <= crc ^ {24'b0,crc_input}; crc_bits <= 8;
                        if (boot_operation == 1) begin
                            written_count <= written_count + 1'b1;
                        end else begin
                            read_data <= {8'b0, boot_odd ? mem_read[15:8] : mem_read[7:0]};
                            verified_count <= verified_count + 1'b1;
                        end
                    end
                    if(boot_operation == 0) begin ready <= request; state <= COMPLETE; end
                    else begin ready <= 0; state <= CRC_BITS; end
                end
                CRC_BITS: begin
                    crc <= crc[0] ? (crc >> 1) ^ 32'hedb88320 : crc >> 1;
                    crc_bits <= crc_bits - 1'b1;
                    if(crc_bits == 1) begin ready <= request; state <= COMPLETE; end
                end
                FDD: if(fdd_ready) begin read_data <= fdd_read; ready <= request; state <= COMPLETE; end
                ROM: begin read_data <= boot_data; ready <= request; state <= COMPLETE; end
                COMPLETE: if (!request) begin ready <= 0; state <= IDLE; end
                UNMAPPED: if (!request) state <= IDLE;
                default: state <= IDLE;
            endcase
            // A newly arriving tick wins over reading/clearing the pending bit.
            if(tick50) begin
                tick_pending <= 1;
            end
            if(frame50) begin
                if(locked && model >= 3 && !video_register[14]) frame_pending <= 1;
            end
        end
    end
endmodule
