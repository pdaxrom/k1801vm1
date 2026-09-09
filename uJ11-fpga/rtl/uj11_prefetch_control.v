`timescale 1ns/1ps
// Control bit 4 pauses speculative launches, preserving sequential READ state.
// ALU words inherit the last control policy; their immediate/page bits are free.
module uj11_prefetch_control(input wire clk,reset,input wire [35:0] uword,output wire allow);
    reg inherited;
    always @(posedge clk) begin
        if(reset) inherited<=0;
        else if(uword[35]) inherited<=!uword[4];
    end
    // FETCH_A1 may transfer PC on the next microinstruction. Suppress this
    // one speculative launch; ordinary ALU/control inheritance is unchanged.
    assign allow=uword[35] ? !uword[4] : (inherited && uword[9:8]!=2'd3);
    wire unused_fields=^{uword[34:10],uword[7:5],uword[3:0]};
endmodule
