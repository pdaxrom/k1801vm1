`timescale 1ns/1ps
// External dispatch stimulus isolates the execution engine from the decoder.
module uj11_probe_engine(input wire clk,reset,serial_in,output wire serial_out);
    reg [39:0] stimulus;
    reg [189:0] observe;
    wire peripheral_reset;
    wire irq_ack,waiting;
    wire [15:0] addr,data,dispatch_ir,ir,mdr,psw,q,rf_data;
    wire request,reading,writing,byte_access,stopped,retire,rf_write;
    wire [1:0] fault;
    wire [3:0] rf_address;
    wire [9:0] upc;
    wire [35:0] uword;
    uj11_engine engine(.irq_valid(stimulus[28]),.irq_priority(stimulus[31:29]),.irq_vector(stimulus[39:32]),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),.clk(clk),.reset(reset),.dispatch_address(stimulus[9:0]),
        .dispatch_ir(dispatch_ir),.mem_addr(addr),.mem_write_data(data),
        .mem_request(request),.mem_read(reading),.mem_write(writing),.mem_byte(byte_access),
        .mem_ack(stimulus[26]),.mem_error(stimulus[27]),.mem_read_data(stimulus[25:10]),
        .stopped(stopped),.fault_code(fault),.retire(retire),.debug_upc(upc),.debug_uword(uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data));
    always @(posedge clk) begin
        if(reset) begin stimulus<=0; observe<=0; end
        else begin
            stimulus <= {stimulus[38:0],serial_in};
            observe <= {peripheral_reset,irq_ack,waiting,addr,data,request,reading,writing,byte_access,dispatch_ir,
                        upc,uword,ir,mdr,psw,q,rf_write,rf_address,rf_data,stopped,fault,retire};
        end
    end
    assign serial_out=^observe;
endmodule
