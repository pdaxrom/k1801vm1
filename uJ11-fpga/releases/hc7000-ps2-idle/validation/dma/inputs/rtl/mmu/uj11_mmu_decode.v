`timescale 1ns/1ps
// J11 decode with MMU-mode controls folded into the opcode groups.
// Kept separate from the HC1200 decoder and its VM2/HALT extensions.
module uj11_mmu_decode(
    input wire [15:0] ir,
    input wire [1:0] mode,
    input wire csm_enabled,fpp_enabled,
    output reg [11:0] entry
);
    wire memory_mode=|ir[5:3];
    wire rr=!(|ir[11:9]) && !memory_mode;
    always @* begin
        entry=10'h042;
        case(ir[15:12])
            4'h1,4'h2,4'h3,4'h4,4'h5,4'h6,
            4'h9,4'ha,4'hb,4'hc,4'hd,4'he:
                entry={2'b01,(ir[15] && ir[14:12]==3'd6),ir[14:12],!rr,3'b0};
            4'h7: case(ir[11:9])
                0,1: entry={5'b00011,ir[9],1'b0,memory_mode,2'b10};
                2,3: entry={5'b00000,2'b11,ir[9],1'b0,!ir[9]};
                4: entry={8'h0b,memory_mode,1'b0};
                5: if(ir[8:5]==0)entry=10'h011;
                7: entry=10'h098;
                default: entry=10'h042;
            endcase
            4'h0,4'h8: case(ir[11:8])
                0: if(ir[15]) entry=10'h060;
                else case(ir[7:6])
                    0: case(ir[5:0])
                        0: entry=mode==0 ? 12'h500 : 12'h040;
                        1: entry=10'h012;
                        2,6: entry=10'h031;
                        3: entry=10'h024;
                        4: entry=10'h026;
                        5: entry=mode==0 ? 12'h021 : 12'h0b9;
                        7: entry=10'h017;
                        default: entry=10'h042;
                    endcase
                    1: entry=memory_mode ? 10'h080 : 10'h040;
                    2: case(ir[5:3])
                        0: entry=10'h090;
                        3: entry=mode==0 ? {6'b011111,ir[2:0],1'b0} : 12'h0b9;
                        4,5,6,7: entry={3'b010,ir[3:0],3'b100};
                        default: entry=10'h042;
                    endcase
                    3: entry={6'b011100,memory_mode,3'b0};
                endcase
                1,2,3,4,5,6,7: entry={4'h1,ir[15],ir[10:8],2'b00};
                8,9: if(ir[15])entry={8'h0a,ir[8],1'b0};
                     else if(memory_mode)entry=10'h088;
                10,11,12: entry={2'b00,ir[11:6],memory_mode,1'b0};
                13: case(ir[7:6])
                    0: entry={6'b011010,ir[15],1'b0,(ir[15] && memory_mode),1'b0};
                    1: entry=12'h400;
                    2: entry=12'h420;
                    3: entry=ir[15] ? {8'h76,memory_mode,1'b0} : {7'b0110110,memory_mode,2'b0};
                    default: entry=10'h042;
                endcase
                14: if(!ir[15])case(ir[7:6])
                    0: if(csm_enabled && mode!=0)entry=12'h440;
                    2: if(memory_mode)entry=12'h470;
                    3: if(memory_mode)entry=12'h478;
                    default:begin end
                endcase
                default: entry=10'h042;
            endcase
            default: entry=10'h042;
        endcase
        if(ir[15:12]==4'hf)entry=fpp_enabled ? 12'h700 : 12'h042;
    end
endmodule
