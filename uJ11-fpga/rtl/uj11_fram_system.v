`timescale 1ns/1ps
// M0 core with board-address-map memory. I/O stays an explicit demand-only port.
// Resolved IRQ input; no boot overlay, MMU or bank extension.
module uj11_fram_system #(
    parameter integer CLK_DIV=1,
    parameter integer MEMORY_MODE=2 // 0 legacy, 1 sequential, 2 sequential+prefetch
) (
    input wire clk, reset,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [8:1] irq_vector,
    output wire irq_ack, waiting, peripheral_reset,
    output wire spi_cs_n, spi_sck, spi_mosi,
    input wire spi_miso,
    output wire io_request, io_write, io_byte,
    output wire [15:0] io_address, io_wdata,
    input wire [15:0] io_rdata,
    input wire io_ack, io_error,
    output wire stopped, retire,
    output wire [1:0] fault_code,
    output wire [9:0] debug_upc,
    output wire [35:0] debug_uword,
    output wire [15:0] ir, mdr, psw, q,
    output wire debug_rf_write,
    output wire [3:0] debug_rf_address,
    output wire [15:0] debug_rf_data,
    output wire memory_request, memory_write, memory_ack
);
    wire [15:0] address,wdata,rdata;
    wire reading,byte_access,error;
    uj11_core core(.clk(clk),.reset(reset),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.mem_addr(address),.mem_write_data(wdata),
        .mem_request(memory_request),.mem_read(reading),.mem_write(memory_write),.mem_byte(byte_access),
        .mem_ack(memory_ack),.mem_error(error),.mem_read_data(rdata),.stopped(stopped),
        .fault_code(fault_code),.retire(retire),.debug_upc(debug_upc),.debug_uword(debug_uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(debug_rf_write),
        .debug_rf_address(debug_rf_address),.debug_rf_data(debug_rf_data));
    generate if(MEMORY_MODE==0) begin : legacy
    uj11_fram_baseline #(.CLK_DIV(CLK_DIV)) memory(
        .clk(clk),.reset(reset),.request(memory_request),.write(memory_write),.byte_access(byte_access),
        .address(address),.wdata(wdata),.rdata(rdata),.ack(memory_ack),.error(error),
        .io_request(io_request),.io_write(io_write),.io_byte(io_byte),.io_address(io_address),.io_wdata(io_wdata),
        .io_rdata(io_rdata),.io_ack(io_ack),.io_error(io_error),
        .spi_cs_n(spi_cs_n),.spi_sck(spi_sck),.spi_mosi(spi_mosi),.spi_miso(spi_miso));
    end else begin : sequential
    wire stream,prefetch_enable;
    uj11_stream stream_hint(.uword(debug_uword),.ir(ir),.stream(stream));
    uj11_prefetch_control prefetch_policy(.clk(clk),.reset(reset),.uword(debug_uword),.allow(prefetch_enable));
    uj11_prefetch #(.CLK_DIV(CLK_DIV),.PREFETCH(MEMORY_MODE==2)) memory(
        .clk(clk),.reset(reset),.request(memory_request),.write(memory_write),.byte_access(byte_access),
        .stream(stream),.prefetch_enable(prefetch_enable),.pc_write(debug_rf_write && debug_rf_address==4'd7),
        .pc_data(debug_rf_data),.stopped(stopped),
        .address(address),.wdata(wdata),.rdata(rdata),.ack(memory_ack),.error(error),
        .io_request(io_request),.io_write(io_write),.io_byte(io_byte),.io_address(io_address),.io_wdata(io_wdata),
        .io_rdata(io_rdata),.io_ack(io_ack),.io_error(io_error),
        .spi_cs_n(spi_cs_n),.spi_sck(spi_sck),.spi_mosi(spi_mosi),.spi_miso(spi_miso));
    end endgenerate
    wire unused_read=reading;
endmodule
