`timescale 1ns/1ps
// uJ11 M0: a small predecoder dispatches directly into the microcoded engine.
// All architectural execution, including PC+2 and BR, uses the shared ALU/RF.
module uj11_core #(parameter integer ROM_DECODE=0, FP11_CONTROL=0, IRQ_VECTOR_BITS=8, parameter [15:0] UNMASKED_VECTOR=0) (
    input wire clk, reset,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [IRQ_VECTOR_BITS:1] irq_vector,
    output wire irq_ack, waiting, peripheral_reset,
    output wire [15:0] mem_addr, mem_write_data,
    output wire mem_request, mem_read, mem_write, mem_byte,
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
    wire cpu_request,cpu_read,cpu_write;
    wire [15:0] fp_rdata;
    // v13 bit1 is legal only on private READ/WRITE and reset JUMP.
    wire fp_tag=FP11_CONTROL!=0 && debug_uword[35] && debug_uword[1];
    wire fp_cycle=fp_tag && debug_uword[34];
    reg fp_ack;
    wire fp_start=cpu_request && fp_cycle && !fp_ack;
    always @(posedge clk) begin
        if(reset)fp_ack<=0;
        else fp_ack<=fp_start;
    end
    assign mem_request=cpu_request && !fp_cycle;
    assign mem_read=cpu_read && !fp_cycle;
    assign mem_write=cpu_write && !fp_cycle;
    generate if(ROM_DECODE!=0)begin: sync_decode
        // The reset microprogram's final JUMP issues a private FPS clear.
        // R0 has already been cleared, so address/data use the ordinary buses.
        wire fp_init=fp_tag && !debug_uword[34];
        wire fp_enable=FP11_CONTROL!=0 && !reset && (fp_start || fp_init);
        wire [4:0] fp_address=mem_addr[5:1];
        wire [15:0] fp_wdata=mem_write_data & ((|fp_address) ? 16'hffff : 16'hcfef);
        wire fetch=debug_uword[35] && debug_uword[34:31]==4'd2;
        uj11_decode_rom #(.FP11_CONTROL(FP11_CONTROL)) decode(.clk(clk),.enable(fetch && mem_request && mem_ack && !mem_error),
            .incoming(mem_read_data),.entry(dispatch_address),
            .fp_enable(fp_enable),.fp_write(!debug_uword[31]),.fp_address(fp_address),
            .fp_wdata(fp_wdata),.fp_rdata(fp_rdata));
    end else begin: logic_decode
        uj11_decode decode(.ir(dispatch_ir),.entry(dispatch_address));
        assign fp_rdata=0;
        // The first FP checkpoint deliberately requires the shared EBR decoder.
        // Logic-decoder configurations keep their established integer/FIS ISA.
`ifndef SYNTHESIS
        initial if(FP11_CONTROL!=0)$fatal(1,"FP11_CONTROL requires ROM_DECODE=1");
`endif
    end endgenerate
    uj11_engine #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(IRQ_VECTOR_BITS),.UNMASKED_VECTOR(UNMASKED_VECTOR)) engine(.clk(clk),.reset(reset),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.dispatch_address(dispatch_address),
        .dispatch_ir(dispatch_ir),.mem_addr(mem_addr),.mem_write_data(mem_write_data),
        .mem_request(cpu_request),.mem_read(cpu_read),.mem_write(cpu_write),.mem_byte(mem_byte),
        .mem_ack(fp_cycle ? fp_ack : mem_ack),.mem_error(!fp_cycle && mem_error),.mem_read_data(fp_cycle ? fp_rdata : mem_read_data),
        .stopped(stopped),.fault_code(fault_code),.retire(retire),.debug_upc(debug_upc),
        .debug_uword(debug_uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(debug_rf_write),.debug_rf_address(debug_rf_address),.debug_rf_data(debug_rf_data));
endmodule
