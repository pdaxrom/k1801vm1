`timescale 1ns/1ps
// 16 stimulus + 14 observation FF, plus 12 controller FF.
module uj11_probe_mmu_entry(input wire clk,reset,serial_in,output wire serial_out);
    reg [15:0] stimulus;
    reg [13:0] observe;
    wire redirect,active,block_memory,stall;
    wire [9:0] address;
    uj11_mmu_entry entry(.clk(clk),.reset(reset),.upc(stimulus[9:0]),
        .enabled(stimulus[10]),.running(stimulus[11]),.memory_word(stimulus[12]),
        .return_word(stimulus[13]),.hold_routine(stimulus[14]),.guest_advance(stimulus[15]),
        .redirect(redirect),.redirect_address(address),.active(active),
        .block_memory(block_memory),.stall(stall));
    always @(posedge clk)begin
        if(reset)begin stimulus<=0;observe<=0;end
        else begin
            stimulus<={stimulus[14:0],serial_in};
            observe<={address & {10{redirect}},redirect,active,block_memory,stall};
        end
    end
    assign serial_out=^observe;
endmodule
