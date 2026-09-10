`timescale 1ns/1ps
// Common CP35 core harness: 32 stimulus and 176 raw observation bits.
// Observe the two extra control bits in both fits to retain identical I/O FF.
module uj11_probe_entry_core(
    input wire clk,reset,serial_in,output wire serial_out);
    reg [31:0] stimulus;
    reg [175:0] observe;
    wire peripheral_reset,irq_ack,waiting;
    wire [15:0] addr,data,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
        uj11_core #(.ROM_DECODE(1)) core(.mmu_enabled(stimulus[30]),.mmu_hold(stimulus[31]),
            .irq_valid(stimulus[18]),.irq_priority(stimulus[21:19]),.irq_vector(stimulus[29:22]),
            .irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),
            .clk(clk),.reset(reset),.mem_addr(addr),.mem_write_data(data),.mem_request(request),
            .mem_read(reading),.mem_write(writing),.mem_byte(byte_access),.mem_ack(stimulus[16]),
            .mem_error(stimulus[17]),.mem_read_data(stimulus[15:0]),.stopped(stopped),
            .fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(uword),
            .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
            .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    always @(posedge clk)begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin
            stimulus<={stimulus[30:0],serial_in};
            observe<={stimulus[31:30],peripheral_reset,irq_ack,waiting,addr,data,request,reading,writing,
                      byte_access,upc,uword,ir,mdr,psw,q,rf_write,rf_address,rf_data,stopped,fault,retire};
        end
    end
    assign serial_out=^observe;
endmodule
