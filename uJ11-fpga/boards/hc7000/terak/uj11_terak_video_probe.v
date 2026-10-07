`timescale 1ns/1ps
// Diagnostic raster, using the production 06B6 timing and DAC levels unchanged.
// Native 640x240 picture: one-pixel border, decimal row labels and 8-line ruler.
module uj11_terak_video_probe #(parameter PAL50=0,FASTER=0,MEASURE=0,PROGRESSIVE=0)(
    input wire clk,reset,
    output reg [5:0] dac
);
    wire [11:0] x;
    wire [10:0] half_line;
    wire [9:0] line_number;
    wire line_start,field_start,field,sync,active_line,active;
    wire [7:0] row;
    // Optional faster serialization preserves all columns and their midpoint.
    localparam X_FIRST=FASTER ? 695 : PAL50 ? 685 : 768;
    localparam FAST_PIXELS=FASTER;
    localparam PICTURE_CLOCKS=FAST_PIXELS ? 3180 : 3200;
    localparam FIRST_LABEL=MEASURE ? 12 : 0, LAST_ROW=MEASURE ? 246 : 239;
    generate if(PAL50)begin:pal50
        // Reuse the existing 625/50 sync generator. The full native picture
        // is centered within the 288-line PAL field, with black margins.
        uj11_pal_timing timing(.clk(clk),.reset(reset),.x(x),.half_line(half_line),
            .line_number(line_number),.line_start(line_start),.frame_start(),
            .field(field),.sync(sync),.burst(),.active_line(),.active(),.row(),.v_alternate());
        assign field_start=(half_line==0 || half_line==625) && x[10:0]==0;
        assign active_line=(line_number>=48 && line_number<288) ||
            (line_number>=361 && line_number<601);
        assign row=field ? line_number-361 : line_number-48;
        assign active=active_line && x>=X_FIRST && x<X_FIRST+PICTURE_CLOCKS;
    end else if(PROGRESSIVE)begin:progressive
        wire native_active_line;
        wire [7:0] native_row;
        uj11_terak_240p_timing timing(.clk(clk),.reset(reset),.x(x),.half_line(half_line),
            .line_number(line_number),.line_start(line_start),.frame_start(),
            .field_start(field_start),.field(field),.sync(sync),.active_line(native_active_line),
            .active(),.prefetch(),.row(native_row),.next_row());
        assign active_line=MEASURE ? (line_number>=12 && line_number<259) : native_active_line;
        assign row=MEASURE ? line_number-12 : native_row;
        assign active=active_line && x>=X_FIRST && x<X_FIRST+PICTURE_CLOCKS;
    end else begin:ntsc
        wire native_active_line;
        wire [7:0] native_row;
        uj11_terak_timing timing(.clk(clk),.reset(reset),.x(x),.half_line(half_line),
            .line_number(line_number),.line_start(line_start),.frame_start(),
            .field_start(field_start),.field(field),.sync(sync),.active_line(native_active_line),
            .active(),.prefetch(),.row(native_row),.next_row());
        // Measure complete physical lines, omitting the half-line field start.
        // Field two's first complete line is global line 263.
        assign active_line=MEASURE ?
            ((line_number>=12 && line_number<259) || (line_number>=275 && line_number<522)) : native_active_line;
        assign row=MEASURE ? (field ? line_number-275 : line_number-12) : native_row;
        assign active=active_line && x>=X_FIRST && x<X_FIRST+PICTURE_CLOCKS;
    end endgenerate
    reg [9:0] column;
    reg [7:0] visible_row;
    // A stepped edge strip separates clipping from a faint one-pixel line.
    // Rows 32..159 carry 16 bands, each labelled with its innermost column.
    wire [8:0] ruler_row={1'b0,visible_row}+FIRST_LABEL;
    wire edge_area=(PAL50 || FASTER) && visible_row>=32 && visible_row<160;
    wire [3:0] edge_index=visible_row[6:3]-4'd4;
    wire [3:0] left_ones=edge_index<10 ? edge_index : edge_index-4'd10;
    wire [3:0] right_ones=edge_index<10 ? 4'd9-edge_index : 5'd19-edge_index;
    reg [2:0] phase;
    reg [11:0] digits,label_digits;
    function [11:0] increment_bcd(input [11:0] value);
        begin
            if(value[3:0]!=9)increment_bcd=value+12'h001;
            else if(value[7:4]!=9)increment_bcd=value+12'h007;
            else increment_bcd=value+12'h067;
        end
    endfunction
    function [34:0] font(input [7:0] code);
        begin
            case(code)
                // Five-bit rows, top row in the least significant five bits.
                "0":font={5'b01110,5'b10001,5'b11001,5'b10101,5'b10011,5'b10001,5'b01110};
                "1":font={5'b01110,5'b00100,5'b00100,5'b00100,5'b00100,5'b01100,5'b00100};
                "2":font={5'b11111,5'b01000,5'b00100,5'b00010,5'b00001,5'b10001,5'b01110};
                "3":font={5'b11110,5'b00001,5'b00001,5'b01110,5'b00001,5'b00001,5'b11110};
                "4":font={5'b00010,5'b00010,5'b11111,5'b10010,5'b01010,5'b00110,5'b00010};
                "5":font={5'b11110,5'b00001,5'b00001,5'b11110,5'b10000,5'b10000,5'b11111};
                "6":font={5'b01110,5'b10001,5'b10001,5'b11110,5'b10000,5'b01000,5'b00110};
                "7":font={5'b01000,5'b01000,5'b01000,5'b00100,5'b00010,5'b00001,5'b11111};
                "8":font={5'b01110,5'b10001,5'b10001,5'b01110,5'b10001,5'b10001,5'b01110};
                "9":font={5'b01100,5'b00010,5'b00001,5'b01111,5'b10001,5'b10001,5'b01110};
                "T":font={5'b00100,5'b00100,5'b00100,5'b00100,5'b00100,5'b00100,5'b11111};
                "O":font={5'b01110,5'b10001,5'b10001,5'b10001,5'b10001,5'b10001,5'b01110};
                "P":font={5'b10000,5'b10000,5'b10000,5'b11110,5'b10001,5'b10001,5'b11110};
                "B":font={5'b11110,5'b10001,5'b10001,5'b11110,5'b10001,5'b10001,5'b11110};
                "M":font={5'b10001,5'b10001,5'b10001,5'b10101,5'b10101,5'b11011,5'b10001};
                default:font=0;
            endcase
        end
    endfunction
    reg [7:0] character;
    reg [2:0] font_row;
    always @*begin
        character=" ";font_row=0;
        if(column>=16 && column<40 && ruler_row[2:0]!=0)begin
            font_row=ruler_row[2:0]-1'b1;
            case(column[5:3])
                2:character="0"+label_digits[11:8];
                3:character="0"+label_digits[7:4];
                4:character="0"+label_digits[3:0];
                default:begin end
            endcase
        end
        if(column>=160 && column<240)begin
            if(visible_row>=2 && visible_row<9)begin
                font_row=visible_row-2;
                case(column[6:3])
                    4:character="T";
                    5:character="O";
                    6:character="P";
                    8:character="0";
                    9:character=MEASURE ? "1" : "0";
                    10:character=MEASURE ? "2" : "0";
                    default:character=" ";
                endcase
            end
            if(visible_row>=LAST_ROW-9 && visible_row<LAST_ROW-2)begin
                font_row=visible_row-(LAST_ROW-9);
                case(column[6:3])
                    4:character="B";
                    5:character="O";
                    6,7:character="T";
                    8:character="O";
                    9:character="M";
                    11:character="2";
                    12:character=MEASURE ? "5" : "3";
                    13:character=MEASURE ? "8" : "9";
                    default:character=" ";
                endcase
            end
        end
        if(edge_area && visible_row[2:0]!=0)begin
            if(column>=80 && column<104)begin
                font_row=visible_row[2:0]-1'b1;
                case(column[6:3])
                    10:character="0";
                    11:character=edge_index<10 ? "0" : "1";
                    12:character="0"+left_ones;
                    default:begin end
                endcase
            end
            if(column>=544 && column<568)begin
                font_row=visible_row[2:0]-1'b1;
                case(column[6:3])
                    4:character="6";
                    5:character=edge_index<10 ? "3" : "2";
                    6:character="0"+right_ones;
                    default:begin end
                endcase
            end
        end
    end
    // Pipeline the font lookup, scan selection and DAC. Sync/blanking take
    // exactly the same three clocks, so their phase relative to the picture
    // is unchanged (the entire composite waveform is delayed together).
    reg [34:0] glyph;
    reg [4:0] glyph_line;
    reg [2:0] scan1,column1,column2;
    reg sync1,sync2,active1,active2,border1,border2,grid1,grid2;
    wire ink=column2<5 && glyph_line[4-column2];
    wire border=visible_row==0 || visible_row==LAST_ROW || column==0 || column==639 ||
        (MEASURE && (ruler_row==18 || ruler_row==257)) ||
        (edge_area && (column<=edge_index || column>=639-edge_index));
    wire grid=ruler_row[2:0]==0 || column[5:0]==0 || visible_row==119 || visible_row==120 || column==319;
    always @(posedge clk)begin
        if(reset)begin
            column<=0;visible_row<=0;phase<=0;digits<=0;label_digits<=0;dac<=0;
            glyph<=0;glyph_line<=0;scan1<=0;column1<=0;column2<=0;
            sync1<=1;sync2<=1;active1<=0;active2<=0;border1<=0;border2<=0;grid1<=0;grid2<=0;
        end
        else begin
            if(line_start)visible_row<=row;
            if(line_start && active_line)begin
                if(row==0)begin digits<=MEASURE ? 12'h012 : 0;label_digits<=MEASURE ? 12'h008 : 0;end
                else begin
                    digits<=increment_bcd(digits);
                    if((MEASURE ? row[2:0]==4 : row[2:0]==0))label_digits<=increment_bcd(digits);
                end
            end
            if(x==X_FIRST-1)begin column<=0;phase<=0;end
            else if(active)begin
                // Twenty columns per line take four clocks, the rest five.
                // The first and last columns keep the full five-clock pulse.
                if(phase==(FAST_PIXELS && column[4:0]==15 ? 3 : 4))begin
                    phase<=0;column<=column+1'b1;
                end
                else phase<=phase+1'b1;
            end
            glyph<=font(character);scan1<=font_row;column1<=column[2:0];
            sync1<=sync;active1<=active;border1<=border;grid1<=grid;
            glyph_line<=glyph>>(scan1*5);column2<=column1;
            sync2<=sync1;active2<=active1;border2<=border1;grid2<=grid1;
            dac<=sync2 ? 6'd0 : active2 ? (border2 || ink ? 6'd50 : grid2 ? 6'd32 : 6'd15) : 6'd15;
        end
    end
endmodule
