`timescale 1ns/1ps
// Two R0-R5 sets, shared PC, per-mode SP, eight microcode temporaries.
// Reset clears every physical slot through the ordinary single write port.
module uj11_mmu_regfile(
    input wire clk, reset, write_enable,
    input wire [3:0] a,b,
    input wire [15:0] write_data,psw,
    input wire previous,pipeline_enabled,
    input wire defer_write,commit_deferred,discard_deferred,
    output wire [15:0] read_a,read_b,
    output reg initialized,
    input wire debug_write,
    input wire [4:0] debug_address,
    input wire [15:0] debug_data,
    output wire [15:0] debug_read_data,pc
);
    wire unused_status=^psw[10:0];
    reg [15:0] words[0:31] /* synthesis syn_ramstyle = "distributed" */;
    reg [4:0] clear_address;
    reg pending;
    reg [4:0] pending_address;
    reg [15:0] pending_data;
    function [4:0] address(input [3:0] r,input [4:0] status,input prev);
        reg [1:0] mode;
        begin
            mode=prev ? status[2:1] : status[4:3];
            if(mode==2)mode=prev ? 2'd3 : 2'd0;
            if(r>=8)address={2'b11,r[2:0]};
            else if(r<6)address={1'b0,status[0],r[2:0]};
            else if(r==6)address={3'b100,mode};
            else address=5'd7;
        end
    endfunction
    // Previous space instructions select the previous SP only. R0-R5 retain
    // the currently selected register set, as do their effective-address bases.
    wire [4:0] aa=address(a,psw[15:11],previous),ba=address(b,psw[15:11],previous);
    wire [15:0] raw_a=pending && aa==pending_address ? pending_data : words[aa];
    wire [15:0] raw_b=pending && ba==pending_address ? pending_data : words[ba];
    reg [15:0] operand_a,operand_b;
    always @(posedge clk)begin operand_a<=raw_a;operand_b<=raw_b;end
    assign read_a=pipeline_enabled ? operand_a : raw_a;
    assign read_b=pipeline_enabled ? operand_b : raw_b;
    assign pc=words[7];
    assign debug_read_data=words[debug_address];
    always @(posedge clk) begin
        if(reset)begin initialized<=0;clear_address<=0;pending<=0;pending_address<=0;pending_data<=0;end
        else if(!initialized)begin
            words[clear_address]<=0;
            if(clear_address==31)initialized<=1;
            else clear_address<=clear_address+1'b1;
        end else begin
            if(discard_deferred || commit_deferred)pending<=0;
            else if(write_enable && defer_write)begin
                pending<=1;pending_address<=ba;pending_data<=write_data;
            end
            if(debug_write)words[debug_address]<=debug_data;
            else if(commit_deferred && pending)words[pending_address]<=pending_data;
            else if(write_enable && !defer_write)words[ba]<=write_data;
        end
    end
endmodule
