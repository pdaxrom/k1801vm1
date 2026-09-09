// Frozen CP27 decoder from commit 0f1047e (module renamed for equivalence).
`timescale 1ns/1ps
// Word ALU classes use direct instruction bits; mode zero bypasses shared EA.
module uj11_decode_gold(input wire [15:0] ir,output wire [9:0] entry);
    wire rr = {ir[11:9],ir[5:3]}==6'b0;
    wire supported = ir[14:12]!=3'd0 && ir[14:12]!=3'd7;
    wire single_operand = ir[14:9]==6'h05 || ir[14:8]==7'h0c;
    wire branch = ir[14:11]==4'b0 && (ir[15] || ir[10:8]!=3'b0);
    wire jump_opcode = ir[15:6]==10'd1;
    wire jump = jump_opcode && (|ir[5:3]);
    wire jsr = ir[15:9]==7'd4 && (|ir[5:3]);
    wire rts = ir[15:3]==13'd16;
    wire ash_pair = ir[15:10]==6'o35; // ASH/ASHC: one class, direct IR[9] dispatch
    wire mul_div = ir[15:10]==6'o34; // direct IR[9] selects MUL or DIV
    wire xor_op = ir[15:9]==7'o74;
    wire fis = ir[15:5]==11'o1720; // 07500R..07503R, four stack operations
    wire sob = ir[15:9]==7'd63;
    wire swab=ir[15:6]==10'd3;
    wire sxt=ir[15:6]==10'd55;
    wire mark_mtps=ir[14:6]==9'd52;
    wire mfps=ir[15:6]==10'o1067;
    // Classes are disjoint: parallel masks avoid a priority chain.
    wire bpt=ir==16'o000003, iot=ir==16'o000004, rti_rtt={ir[15:3],ir[1:0]}==15'd2;
    wire wait_op=ir==16'o000001, spl=ir[15:3]==13'd19;
    wire halt_op=ir==16'o000000, reset_op=ir==16'o000005;
    wire cc=ir[15:5]==11'd5, mfpt=ir==16'o000007;
    wire emt_trap=ir[15:9]==7'h44;
    wire valid = fis | mul_div | xor_op | ash_pair | supported | single_operand | branch | jump | jsr | rts | sob | swab | sxt | mark_mtps | mfps | bpt | iot | rti_rtt | emt_trap | wait_op | spl | cc | mfpt | halt_op | reset_op;
    assign entry = ({10{mul_div}} & {5'b00011,ir[9],1'b0,(|ir[5:3]),2'b10}) | ({10{xor_op}} & {8'h0b,(|ir[5:3]),1'b0}) |
                   ({10{ash_pair}} & {5'b00000,2'b11,ir[9],1'b0,!ir[9]}) | ({10{supported}} & {2'b01,(ir[15] && ir[14:12]==3'd6),ir[14:12],!rr,3'b0}) |
                   ({10{single_operand}} & {2'b00,ir[11:6],(|ir[5:3]),1'b0}) |
                   ({10{branch}} & {4'h1,ir[15],ir[10:8],2'b00}) |
                   ({10{jump}} & 10'h080) | ({10{jsr}} & 10'h088) |
                   ({10{rts}} & 10'h090) | ({10{sob}} & 10'h098) |
                   ({10{swab}} & {6'b011100,(|ir[5:3]),3'b0}) |
                   ({10{sxt}} & {7'b0110110,(|ir[5:3]),2'b0}) |
                   ({10{mark_mtps}} & {6'b011010,ir[15],1'b0,(ir[15] && (|ir[5:3])),1'b0}) |
                   ({10{mfps}} & {8'h76,(|ir[5:3]),1'b0}) | ({10{bpt}} & 10'h024) |
                   ({10{iot}} & 10'h026) | ({10{rti_rtt}} & 10'h031) |
                   ({10{emt_trap}} & {8'h0a,ir[8],1'b0}) |
                   ({10{halt_op}} & 10'h018) | ({10{reset_op}} & 10'h021) |
                   ({10{wait_op}} & 10'h012) | ({10{mfpt}} & 10'h017) | ({10{fis}} & 10'h011) |
                   ({10{cc}} & {3'b010,ir[3:0],3'b100}) |
                   ({10{spl}} & {6'b011111,ir[2:0],1'b0}) | ({10{!valid}} & {8'h10,!jump_opcode,1'b0});
endmodule
