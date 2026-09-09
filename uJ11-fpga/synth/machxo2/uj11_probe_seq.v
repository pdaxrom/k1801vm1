// Four-pin resource harness, not a board CPU. 45 input FF + one checksum FF
// keep all sequencer inputs and all ROM output lanes observable on SG32.
// Report its overhead separately; do not call total MAP resources bare core.
`timescale 1ns/1ps
module uj11_probe_seq(input wire clk, reset, serial_in, output reg serial_out);
    reg [44:0] stimulus;
    wire [35:0] uword;
    wire [9:0] upc, next_address;
    wire run = stimulus[29];
    uj11_microseq seq(.trace_pending(stimulus[44]),.irq_pending(stimulus[41]),.fault_redirect(stimulus[42]),.fault_repair(stimulus[43]),
        .clk(clk), .reset(reset), .enable(run), .uword(uword),
        .ir(stimulus[15:0]), .nzvc(stimulus[19:16]),
        .dispatch_address(stimulus[39:30]), .address_odd(stimulus[20]),
        .byte_instruction(stimulus[21]), .selected_a(stimulus[25:22]),
        .q0(stimulus[26]), .loop_zero(stimulus[27]), .bus_error(stimulus[28]), .a_one(stimulus[40]),
        .upc(upc), .next_address(next_address)
    );
    uj11_rom rom(.clk(clk), .enable(reset | run),
                 .address(next_address), .data(uword));
    always @(posedge clk) begin
        if (reset) begin
            stimulus <= 0;
            serial_out <= 0;
        end else begin
            stimulus <= {stimulus[43:0], serial_in};
            serial_out <= ^{uword, upc, next_address};
        end
    end
endmodule
