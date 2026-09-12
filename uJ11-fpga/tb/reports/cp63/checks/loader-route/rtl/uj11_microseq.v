// uJ11 encoding v10: synchronous microstore next-address sequencer.
// All side effects, including link writes, obey the same enable as the ROM.
`timescale 1ns/1ps
module uj11_microseq (
    input wire clk, reset, enable,
    input wire [35:0] uword,
    input wire [15:0] ir,
    input wire [3:0] nzvc,
    input wire [9:0] dispatch_address, fault_target,
    input wire step_return, debug_pending, wait_return,
    input wire address_odd, byte_instruction,
    input wire [3:0] selected_a,
    input wire q0, loop_zero, bus_error, a_one, irq_pending, trace_pending, fault_redirect, fault_repair,
    output reg [9:0] upc,
    output reg [9:0] next_address
);
    localparam [9:0] FETCH = 10'h020, IRQ = 10'h013, STOP = 10'h3ff;
    wire [9:0] fetch_address = debug_pending ? 10'h053 : trace_pending ? 10'h024 : irq_pending ? IRQ : FETCH;
    reg [9:0] link;
    reg link_valid;
    wire [9:0] sequential = upc + 10'd1;
    wire [3:0] command = uword[34:31];
    wire [9:0] target = uword[20:11];
    wire [7:0] predicates = {bus_error, loop_zero, q0,
                            nzvc[3], nzvc[2], nzvc[1], nzvc[0], 1'b1};
    wire condition = predicates[uword[9:7]] ^ uword[10];
    // These fields belong to datapath/decode; the sequencer deliberately
    // does not interpret them. Keep lint strict for all other signals.
    wire unused_fields = ^{uword[30:21], ir[15:12], ir[8:6], ir[2:0],
                           selected_a[3], selected_a[0]};
    // All OR-dispatches share the same target. Only its low three bits
    // vary; other control operations override this common address below.
    wire [2:0] dispatch_bits =
        (ir[11:9] & {3{command==4'd5}}) |
        (ir[5:3] & {3{command==4'd6}}) |
        ({1'b0,ir[5:3]==3'b0,ir[11:9]==3'b0} & {3{command==4'd7}}) |
        ({1'b0,address_odd,byte_instruction} & {3{command==4'd8}}) |
        ({2'b0,~(selected_a[2] & selected_a[1])} & {3{command==4'd9}});
    always @* begin
        next_address = {target[9:3],target[2:0] | dispatch_bits};
        if (uword[35]) begin
            case (command)
                4'd1: if (!condition) next_address = sequential;
                4'd10: if (link_valid) next_address = STOP;
                4'd3: next_address = link_valid ? link : STOP;
                4'd4, 4'd2: next_address = dispatch_address;
                4'd13: next_address = upc;
                4'd14: next_address = (debug_pending || trace_pending || irq_pending) ? fetch_address : upc;
                default: begin end // JUMP/TRAP, OR_* and memory continuations
            endcase
        end else begin
            case (uword[9:8])
                2'd0: next_address = sequential;
                2'd1: next_address = {upc[9:8], uword[7:0]};
                2'd2: next_address = fetch_address;
                2'd3: next_address = a_one ? fetch_address : sequential;
            endcase
        end
        // STEP returns directly to FETCH, bypassing this one boundary's
        // trace and IRQ arbitration. It does not set T or suppress later faults.
        if (step_return) next_address = FETCH;
        if (wait_return) next_address = 10'h012;
        if (fault_redirect) next_address = uword[2] ? target : fault_target;
        if (fault_repair) next_address = fault_target;
        if (reset)
            next_address = 10'b0;
    end
    always @(posedge clk) begin
        if (reset) begin
            upc <= 0;
            link <= 0;
            link_valid <= 0;
        end else if (enable) begin
            upc <= next_address;
            if (fault_redirect) link_valid <= 0;
            else if (uword[35] && command == 4'd10 && !link_valid) begin
                link <= sequential;
                link_valid <= 1;
            end else if (uword[35] && command == 4'd3)
                link_valid <= 0;
        end
    end
endmodule
