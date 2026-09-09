`timescale 1ns/1ps
// Measured reference adapter around the unchanged lsi11-fpga FRAM controller.
// Bank is hard-wired to zero; the CPU address remains exactly 16 bits.
module uj11_fram_baseline #(
    parameter integer CLK_DIV=1
) (
    input wire clk, reset, request, write, byte_access,
    input wire [15:0] address, wdata,
    output wire [15:0] rdata,
    output wire ack, error,
    output wire io_request, io_write, io_byte,
    output wire [15:0] io_address, io_wdata,
    input wire [15:0] io_rdata,
    input wire io_ack, io_error,
    output wire spi_cs_n, spi_sck, spi_mosi,
    input wire spi_miso
);
    wire io_page=&address[15:13];
    wire [15:0] fram_data;
    wire ready, fram_error, busy;
    assign io_request=request && io_page && !reset;
    assign io_write=write;
    assign io_byte=byte_access;
    assign io_address=address;
    assign io_wdata=wdata;
    assign rdata=io_page ? io_rdata : fram_data;
    assign ack=request && (io_page ? io_ack : ready);
    assign error=io_page ? io_error : fram_error;
    // Legacy request_seen needs req low for one sampled edge after a beat.
    // Gating with ready supplies that edge even for back-to-back core requests.
    spi_fram_guest_ram #(.CLK_DIV(CLK_DIV)) transport(
        .clk(clk),.rst(reset),.req(request && !io_page && !ready),
        .write(write),.byte_access(byte_access),.bank(1'b0),.address(address),.wdata(wdata),
        .rdata(fram_data),.ready(ready),.error(fram_error),.busy(busy),
        .spi_cs_n(spi_cs_n),.spi_sck(spi_sck),.spi_mosi(spi_mosi),.spi_miso(spi_miso));
    wire unused_busy=busy;
endmodule
