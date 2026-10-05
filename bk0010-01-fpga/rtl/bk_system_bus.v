`timescale 1ns/1ps
// System-domain native CSR, SPI and retained ROM bootstrap ports.
module bk_system_bus (
    input wire clk, power_on, reset, peripheral_reset,
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
    output wire sd_cs, sd_sck, sd_mosi,
    input wire sd_miso
);
    localparam IDLE = 0, MEMORY = 1, ROM = 2, COMPLETE = 3, UNMAPPED = 4;
    reg [2:0] state = IDLE;
    reg memory_write = 0;
    reg [1:0] boot_operation = 0;
    reg boot_odd = 0;
    reg [15:0] pointer = 16'h8000;
    reg [15:0] written_count = 0, verified_count = 0;
    reg [31:0] crc = 32'hffffffff, expected_crc = 0;
    reg key_pending = 0, irq_pending = 0, irq_disabled = 0;
    reg [7:0] key_latch = 0;
    reg spi_write = 0;
    reg [2:0] spi_address = 0;
    reg [7:0] spi_data = 0;
    wire [7:0] spi_result, byte_data, byte_divider, byte_rx;
    wire spi_busy, byte_busy, byte_done, byte_start;
    wire [15:0] word_address = {address[15:1], 1'b0};
    assign key_ready = !key_pending;
    assign keyboard_irq = irq_pending && !irq_disabled;
    assign keyboard_vector = key_latch[7] ? 16'o274 : 16'o60;
    assign mem_request = state == MEMORY;
    assign mem_write = memory_write;
    boot_spi_registers spi_regs (
        .clk(clk), .reset(power_on || reset), .enabled(!locked),
        .bus_write(spi_write), .address(spi_address), .data(spi_data),
        .result(spi_result), .busy(spi_busy), .cs(sd_cs),
        .byte_busy(byte_busy), .byte_done(byte_done), .byte_rx(byte_rx),
        .byte_start(byte_start), .byte_data(byte_data), .byte_divider(byte_divider)
    );
    spi_byte_master shifter (
        .clk(clk), .reset(power_on || reset), .start(byte_start),
        .tx_data(byte_data), .divider(byte_divider), .miso(sd_miso),
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
    function [31:0] crc_byte;
        input [31:0] previous;
        input [7:0] value;
        reg [31:0] c;
        integer bit_number;
        begin
            c = previous ^ {24'b0, value};
            for (bit_number = 0; bit_number < 8; bit_number = bit_number + 1)
                c = c[0] ? (c >> 1) ^ 32'hedb88320 : c >> 1;
            crc_byte = c;
        end
    endfunction
    always @(posedge clk) begin
        spi_write <= 0;
        if (power_on) begin
            locked <= 0; boot_error <= 0; boot_stage <= 0;
            pointer <= 16'h8000; written_count <= 0; verified_count <= 0;
            crc <= 32'hffffffff; expected_crc <= 0;
        end
        if (power_on || reset) begin
            state <= IDLE; ready <= 0; read_data <= 0;
            mem_address <= 0; mem_data <= 0; mem_lanes <= 0;
            memory_write <= 0; boot_operation <= 0; boot_odd <= 0;
            key_pending <= 0; irq_pending <= 0; irq_disabled <= 0; key_latch <= 0;
            scroll <= 16'o001330; spi_data <= 0;
        end else begin
            if (peripheral_reset) begin
                key_pending <= 0; irq_pending <= 0; irq_disabled <= 0;
                scroll <= 16'o001330;
            end else begin
                if (irq_ack) irq_pending <= 0;
                if (key_valid && key_ready) begin
                    key_latch <= key_code; key_pending <= 1; irq_pending <= 1;
                end
            end
            case (state)
                IDLE: if (request) begin
                    boot_operation <= 0;
                    if (address < 16'o100000 || (locked && address < 16'o177600 && !write)) begin
                        mem_address <= {5'b0, address[15:1]};
                        memory_write <= write; mem_data <= write_data; mem_lanes <= lanes;
                        state <= MEMORY;
                    end else if (!locked && address >= 16'o100000 && address < 16'o120000 && !write) begin
                        state <= ROM;
                    end else if (word_address == 16'o177660) begin
                        read_data <= {8'b0, key_pending, irq_disabled, 6'b0};
                        if (write && lanes[0]) irq_disabled <= write_data[6];
                        ready <= 1; state <= COMPLETE;
                    end else if (word_address == 16'o177662) begin
                        read_data <= {9'b0, key_latch[6:0]};
                        if (!write && lanes[0]) begin key_pending <= 0; irq_pending <= 0; end
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
                    end else if (!locked && word_address >= 16'o177610 && word_address <= 16'o177636) begin
                        ready <= 1; state <= COMPLETE;
                        case (word_address)
                            16'o177610: begin
                                read_data <= {locked, boot_error, 6'b0, boot_stage};
                                if (write && write_data == 0) begin
                                    boot_error <= 0; written_count <= 0; verified_count <= 0;
                                    crc <= 32'hffffffff; expected_crc <= 0; pointer <= 16'h8000;
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
                                if (pointer < 16'h8000 || (word_address == 16'o177616 && !write) ||
                                        (word_address == 16'o177620 && write)) begin
                                    boot_error <= 1;
                                end else begin
                                    mem_address <= {5'b0, pointer[15:1]};
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
                                if (write && lanes[0]) crc <= crc_byte(crc, write_data[7:0]);
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
                                    if (!boot_error && written_count == 16'h8000 && verified_count == 16'h8000 &&
                                            ~crc == expected_crc) begin locked <= 1; boot_stage <= 8'hff; end
                                    else boot_error <= 1;
                                end
                            end
                            default: read_data <= 0;
                        endcase
                    end else state <= UNMAPPED;
                end
                MEMORY: if (mem_ready) begin
                    read_data <= mem_read;
                    if (boot_operation != 0) begin
                        pointer <= pointer + 1'b1;
                        if (boot_operation == 1) begin
                            crc <= crc_byte(crc, mem_data[7:0]);
                            written_count <= written_count + 1'b1;
                        end else begin
                            read_data <= {8'b0, boot_odd ? mem_read[15:8] : mem_read[7:0]};
                            crc <= crc_byte(crc, boot_odd ? mem_read[15:8] : mem_read[7:0]);
                            verified_count <= verified_count + 1'b1;
                        end
                    end
                    ready <= request; state <= COMPLETE;
                end
                ROM: begin read_data <= boot_data; ready <= request; state <= COMPLETE; end
                COMPLETE: if (!request) begin ready <= 0; state <= IDLE; end
                UNMAPPED: if (!request) state <= IDLE;
                default: state <= IDLE;
            endcase
        end
    end
endmodule
