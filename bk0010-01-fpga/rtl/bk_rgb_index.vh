// Generated from config/video.json by tools/prepare_pal.py.
function [3:0] bk_rgb_index;
    input [7:0] rgb;
    begin case(rgb)
        8'h00: bk_rgb_index = 4'd0;
        8'h03: bk_rgb_index = 4'd1;
        8'h1c: bk_rgb_index = 4'd2;
        8'h1f: bk_rgb_index = 4'd3;
        8'h5c: bk_rgb_index = 4'd4;
        8'h60: bk_rgb_index = 4'd5;
        8'h63: bk_rgb_index = 4'd6;
        8'ha0: bk_rgb_index = 4'd7;
        8'ha3: bk_rgb_index = 4'd8;
        8'hbc: bk_rgb_index = 4'd9;
        8'he0: bk_rgb_index = 4'd10;
        8'he3: bk_rgb_index = 4'd11;
        8'hfc: bk_rgb_index = 4'd12;
        8'hff: bk_rgb_index = 4'd13;
        default: bk_rgb_index = 0;
    endcase end
endfunction
