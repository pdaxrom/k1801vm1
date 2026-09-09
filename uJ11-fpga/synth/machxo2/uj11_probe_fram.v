`timescale 1ns/1ps
// Serial stimulus / raw observation registers. Includes CPU + FRAM transport,
// but not UART/timer/panel/SD. This is a resource probe, not a board pinout.
module uj11_probe_fram #(parameter integer MEMORY_MODE=0)(input wire clk,reset,serial_in,output wire serial_out);
    reg [30:0] stimulus;
    reg [178:0] observe;
    wire peripheral_reset;
    wire irq_ack,waiting;
    wire cs_n,sck,mosi,io_request,io_write,io_byte,stopped,retire,rf_write;
    wire memory_request,memory_write,memory_ack;
    wire [15:0] io_address,io_wdata,ir,mdr,psw,q,rf_data;
    wire [1:0] fault;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [3:0] rf_address;
    uj11_fram_system #(.MEMORY_MODE(MEMORY_MODE)) system(.irq_valid(stimulus[19]),.irq_priority(stimulus[22:20]),.irq_vector(stimulus[30:23]),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.clk(clk),.reset(reset),.spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),
        .spi_miso(stimulus[18]),.io_request(io_request),.io_write(io_write),.io_byte(io_byte),
        .io_address(io_address),.io_wdata(io_wdata),.io_rdata(stimulus[15:0]),
        .io_ack(stimulus[16]),.io_error(stimulus[17]),.stopped(stopped),.retire(retire),
        .fault_code(fault),.debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data),
        .memory_request(memory_request),.memory_write(memory_write),.memory_ack(memory_ack));
    always @(posedge clk) begin
        if(reset) begin stimulus<=0; observe<=0; end
        else begin
            stimulus<={stimulus[29:0],serial_in};
            observe<={peripheral_reset,irq_ack,waiting,cs_n,sck,mosi,io_request,io_write,io_byte,io_address,io_wdata,
                      stopped,retire,fault,upc,uword,ir,mdr,psw,q,rf_write,rf_address,rf_data,
                      memory_request,memory_write,memory_ack};
        end
    end
    assign serial_out=^observe;
endmodule
