`timescale 1ns/1ps
// READ's stream hint is conditional on its selected register actually being PC.
// The same mode-2/3 microcode serves all eight registers without prefetching data.
module uj11_stream(input wire [35:0] uword,input wire [15:0] ir,output wire stream);
    wire [4:0] a=uword[30:26];
    wire pc=(a==5'd7) || (a==5'd16 && ir[8:6]==3'd7) ||
                        (a==5'd17 && ir[2:0]==3'd7);
    assign stream=uword[35] && (uword[34:31]==4'd2 ||
                  (uword[34:31]==4'd11 && uword[5] && pc));
    wire unused_fields=^{uword[25:6],uword[4:0],ir[15:9],ir[5:3]};
endmodule
