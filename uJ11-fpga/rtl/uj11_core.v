`timescale 1ns/1ps
// uJ11 M0: a small predecoder dispatches directly into the microcoded engine.
// All architectural execution, including PC+2 and BR, uses the shared ALU/RF.
module uj11_core #(parameter integer ROM_DECODE=0, ALIGNED_WORD_READS=0, IRQ_VECTOR_BITS=8, parameter [15:0] UNMASKED_VECTOR=0) (
    input wire clk, reset,
    input wire halt_button, debug_block,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [IRQ_VECTOR_BITS:1] irq_vector,
    output wire irq_ack, waiting, peripheral_reset,
    output wire [15:0] mem_addr, mem_write_data,
    output wire mem_request, mem_read, mem_write, mem_byte,
    output wire mem_bank, mem_physical,
    input wire mem_ack, mem_error,
    input wire [15:0] mem_read_data,
    output wire stopped,
    output wire [1:0] fault_code,
    output wire retire,
    output wire [9:0] debug_upc,
    output wire [35:0] debug_uword,
    output wire [15:0] ir, mdr, psw, q,
    output wire debug_rf_write,
    output wire [3:0] debug_rf_address,
    output wire [15:0] debug_rf_data
);
    wire [15:0] dispatch_ir;
    wire [9:0] dispatch_address;
    // A board may return an aligned word for every read. FETCH always reads
    // a word; keep its ROM input ahead of the operand-only byte-lane mux.
    // The default preserves the established right-justified core interface.
    wire [15:0] operand_read_data = (ALIGNED_WORD_READS!=0 && mem_byte) ?
        (mem_addr[0] ? {8'b0,mem_read_data[15:8]} : {8'b0,mem_read_data[7:0]}) : mem_read_data;
    generate if(ROM_DECODE!=0)begin: sync_decode
        wire fetch=debug_uword[35] && debug_uword[34:31]==4'd2;
        uj11_decode_rom decode(.clk(clk),.enable(fetch && mem_request && mem_ack && !mem_error),
            .incoming(mem_read_data),.entry(dispatch_address));
    end else begin: logic_decode
        uj11_decode decode(.ir(dispatch_ir),.entry(dispatch_address));
    end endgenerate
    uj11_engine #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(IRQ_VECTOR_BITS),.UNMASKED_VECTOR(UNMASKED_VECTOR)) engine(.clk(clk),.reset(reset),
        .halt_button(halt_button),.debug_block(debug_block),.irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.dispatch_address(dispatch_address),
        .dispatch_ir(dispatch_ir),.mem_addr(mem_addr),.mem_write_data(mem_write_data),
        .mem_bank(mem_bank),.mem_physical(mem_physical),.mem_request(mem_request),.mem_read(mem_read),.mem_write(mem_write),.mem_byte(mem_byte),
        .mem_ack(mem_ack),.mem_error(mem_error),.mem_read_data(operand_read_data),
        .stopped(stopped),.fault_code(fault_code),.retire(retire),.debug_upc(debug_upc),
        .debug_uword(debug_uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(debug_rf_write),.debug_rf_address(debug_rf_address),.debug_rf_data(debug_rf_data));
endmodule
