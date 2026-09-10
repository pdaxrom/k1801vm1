`timescale 1ns/1ps
// CP35: automatic microcode excursion before one guest memory word.
// No translation/MMR/abort implementation. The normal CALL link is untouched.
// guest_advance means the resumed word advances the CPU, including its fault
// continuation. A physical FETCH ack alone is not enough with ROM_DECODE=1.
module uj11_mmu_entry #(parameter [9:0] ENTRY=10'h1cd)(
    input wire clk, reset, enabled, running, memory_word, return_word,
    input wire hold_routine, guest_advance, input wire [9:0] upc,
    output wire redirect, output wire [9:0] redirect_address,
    output wire active, block_memory, stall);
    localparam IDLE=2'b00, ROUTINE=2'b01, RESUME=2'b10;
    reg [1:0] phase;
    reg [9:0] saved_upc;
    wire enter=running && enabled && memory_word && phase==IDLE;
    wire returning=running && active && return_word && !hold_routine;
    assign active=phase[0];
    assign stall=active && hold_routine;
    assign block_memory=enter || active;
    assign redirect=enter || returning;
    assign redirect_address=active ? saved_upc : ENTRY;
    // saved_upc needs no reset: a ROUTINE can only follow a captured entry.
    always @(posedge clk)begin
        if(reset)phase<=IDLE;
        else if(enter)begin phase<=ROUTINE;saved_upc<=upc;end
        else if(returning)phase<=RESUME;
        else if(running && guest_advance && phase[1])phase<=IDLE;
    end
endmodule
