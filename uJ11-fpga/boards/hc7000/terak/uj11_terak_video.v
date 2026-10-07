`timescale 1ns/1ps
// Native Terak text/font/graphics compositor, presented by the HC7000 PAL DAC.
// The CPU owns the page/font aperture; graphics are fetched live from main RAM.
// One EBR holds two completed packed lines. Mailbox data stays stable through ACK.
module uj11_terak_video #(
    parameter [19:0] PAGE_BASE=20'h08000, FONT_BASE=20'h08800
)(
    input wire clk,video_clk,reset,
    input wire [15:0] gar,vir,vcr,
    output wire memory_request,
    output wire memory_display,memory_font,
    output reg [19:0] memory_address,
    input wire memory_ready,
    input wire [15:0] memory_data,
    output reg [5:0] dac,
    output reg [15:0] underruns,
    output reg field_toggle /* synthesis syn_preserve=1 */
);
    reg [2:0] sp=7,vp=7;
    always @(posedge clk or posedge reset)if(reset)sp<=7;else sp<={sp[1:0],1'b0};
    always @(posedge video_clk or posedge reset)if(reset)vp<=7;else vp<={vp[1:0],1'b0};
    wire srst=sp[2],vrst=vp[2];
    wire [11:0] x;
    wire [10:0] half_line;
    wire [9:0] line_number;
    wire line_start,field_start,field,sync,active_line,active,prefetch;
    wire [7:0] row,next_row;
    uj11_terak_timing timing(.clk(video_clk),.reset(vrst),.x(x),.half_line(half_line),
        .line_number(line_number),.line_start(line_start),.frame_start(),.field_start(field_start),.field(field),
        .sync(sync),.active_line(active_line),.active(active),.row(row),.prefetch(prefetch),.next_row(next_row));
    // The guest line clock follows both field starts, including the mid-line
    // start of the second field. It runs even when VCR blanks the picture.
    always @(posedge video_clk or posedge reset)begin
        if(reset)field_toggle<=0;
        else if(vrst)field_toggle<=0;
        else if(field_start)field_toggle<=!field_toggle;
    end
    reg line_toggle,line_done,fill_bank;
    reg [7:0] fill_row;
    reg [1:0] done_sync /* synthesis syn_preserve=1 */;
    reg [1:0] line_sync /* synthesis syn_preserve=1 */;
    reg done_seen;
    reg [1:0] bank_valid;
    reg [7:0] tag0,tag1;
    reg display_valid,display_bank;
    reg [7:0] visible_row;
    reg visible_line;
    reg underrun_toggle;
    always @(posedge video_clk)begin
        if(vrst)begin
            line_toggle<=0;fill_row<=0;fill_bank<=0;done_sync<=0;done_seen<=0;
            bank_valid<=0;tag0<=0;tag1<=0;display_valid<=0;display_bank<=0;underrun_toggle<=0;
            visible_row<=0;visible_line<=0;
        end else begin
            done_sync<={done_sync[0],line_done};
            if(done_sync[1]!=done_seen)begin done_seen<=done_sync[1];bank_valid[fill_bank]<=1;end
            if(field_start)bank_valid<=0;
            if(line_start)begin
                display_bank<=row[0];
                visible_row<=row;visible_line<=active_line;
            end
            if(x==1)begin
                display_valid<=visible_line && bank_valid[visible_row[0]] &&
                    (visible_row[0] ? tag1==visible_row : tag0==visible_row);
                if(visible_line && !(bank_valid[visible_row[0]] &&
                    (visible_row[0] ? tag1==visible_row : tag0==visible_row)))
                    underrun_toggle<=!underrun_toggle;
            end
            if(x==32 && prefetch && line_toggle==done_sync[1])begin
                fill_row<=next_row;fill_bank<=next_row[0];bank_valid[next_row[0]]<=0;
                if(next_row[0])tag1<=next_row;else tag0<=next_row;
                line_toggle<=!line_toggle;
            end
        end
    end
    localparam IDLE=0,GRAPH=1,PAGE=2,FONT=3,PACK=4,GAP=5,COMPLETE=6,SETUP=7,DIVIDE=8;
    reg [3:0] state,next_state;
    assign memory_display=state==PAGE || state==FONT;
    assign memory_font=state==FONT;
    reg [7:0] current_row /* synthesis syn_preserve=1 */;
    reg current_bank /* synthesis syn_preserve=1 */;
    reg [8:0] remainder;
    reg [4:0] quotient;
    reg [4:0] group_index;
    reg [1:0] glyph_index,pair_index;
    reg [15:0] graphics,characters;
    reg [7:0] glyph,code;
    reg [4:0] text_row;
    reg [3:0] scan;
    reg text_enable,graphics_enable;
    reg [15:0] graphics_base;
    reg font_odd;
    reg [1:0] underrun_sync /* synthesis syn_preserve=1 */;
    reg underrun_seen;
    wire [1:0] zone=current_row<80 ? 2'd2 : current_row<160 ? 2'd1 : 2'd0;
    assign memory_request=state==GRAPH || state==PAGE || state==FONT;
    wire gp=graphics[{glyph_index,pair_index}];
    wire [3:0] pixel0=glyph[{pair_index,1'b0}] ? 4'd2 : gp ? 4'd1 : 4'd0;
    wire [3:0] pixel1=glyph[{pair_index,1'b1}] ? 4'd2 : gp ? 4'd1 : 4'd0;
    wire [9:0] write_address=(current_bank ? 10'd320 : 10'd0)+{1'b0,group_index,glyph_index,pair_index};
    wire [7:0] ram_data;
    reg [3:0] pixel_phase;
    reg [8:0] byte_index;
    reg [7:0] packed_byte;
    reg [3:0] pixel;
    reg short_pair;
    wire ram_read=display_valid && x>=692 && x<3872 && pixel_phase==0;
    uj11_pal_ram line_ram(.write_clk(clk),.write_enable(state==PACK && !srst),
        .write_address(write_address),.write_data({pixel1,pixel0}),
        .read_clk(video_clk),.read_enable(ram_read),
        .read_address((display_bank ? 10'd320 : 10'd0)+{1'b0,byte_index}),.read_data(ram_data));
    always @(posedge video_clk)begin
        if(vrst)begin pixel_phase<=0;byte_index<=0;packed_byte<=0;pixel<=0;dac<=0;short_pair<=0;end
        else begin
            if(x==691)begin pixel_phase<=0;byte_index<=0;pixel<=0;short_pair<=0;end
            else if(x>=692 && x<3875)begin
                // Packed bytes contain two pixels. Shorten only columns
                // 15,47,...,623 to four clocks; every other pixel keeps five.
                // The flag uses the fetched address before byte_index advances.
                pixel_phase<=pixel_phase==(short_pair ? 8 : 9) ? 4'd0 : pixel_phase+1'b1;
                if(pixel_phase==1)begin
                    packed_byte<=ram_data;short_pair<=byte_index[3:0]==7;byte_index<=byte_index+1'b1;
                end
                if(pixel_phase==2)pixel<=packed_byte[3:0];
                if(pixel_phase==7)pixel<=packed_byte[7:4];
            end
            // Same HC7000 resistor DAC calibration as the existing PAL encoder.
            dac<=sync ? 6'd0 : active && display_valid ? (pixel==2 ? 6'd50 : pixel==1 ? 6'd32 : 6'd15) : 6'd15;
        end
    end
    always @(posedge clk)begin
        if(srst)begin
            line_sync<=0;line_done<=0;state<=IDLE;next_state<=IDLE;
            group_index<=0;glyph_index<=0;pair_index<=0;graphics<=0;characters<=0;glyph<=0;code<=0;
            text_row<=0;scan<=0;text_enable<=0;graphics_enable<=0;graphics_base<=0;font_odd<=0;
            current_row<=0;current_bank<=0;remainder<=0;quotient<=0;
            memory_address<=0;underrun_sync<=0;underrun_seen<=0;underruns<=0;
        end else begin
            line_sync<={line_sync[0],line_toggle};
            underrun_sync<={underrun_sync[0],underrun_toggle};
            if(underrun_sync[1]!=underrun_seen)begin
                underrun_seen<=underrun_sync[1];if(underruns!=65535)underruns<=underruns+1'b1;
            end
            case(state)
                IDLE:if(line_sync[1]!=line_done)begin
                    // Held mailbox, sampled after two toggle synchronizer clocks.
                    current_row<=fill_row;current_bank<=fill_bank;state<=SETUP;
                end
                SETUP:begin
                    remainder<={1'b0,current_row}+{5'b0,vir[3:0]};
                    quotient<=vir[12:8]>=25 ? vir[12:8]-25 : vir[12:8];
                    text_enable<=vcr[zone];graphics_enable<=vcr[{1'b0,zone}+3'd3];
                    graphics_base<=gar;group_index<=0;glyph_index<=0;pair_index<=0;
                    memory_address<={5'b0,gar[15:1]}+current_row*20;state<=DIVIDE;
                end
                DIVIDE:if(remainder>=10)begin
                    remainder<=remainder-9'd10;quotient<=quotient==24 ? 5'd0 : quotient+1'b1;
                end else begin
                    text_row<=quotient;scan<=remainder[3:0];
                    if(graphics_enable)state<=GRAPH;
                    else begin graphics<=0;state<=GAP;next_state<=PAGE;end
                end
                GRAPH:if(memory_ready)begin graphics<=memory_data;state<=GAP;next_state<=PAGE;end
                GAP:begin
                    state<=next_state;
                    if(next_state==PAGE)begin
                        if(text_enable)memory_address<=PAGE_BASE+{9'b0,text_row,6'b0}+{14'b0,group_index,1'b0}+(glyph_index>>1);
                        else begin glyph<=0;state<=PACK;pair_index<=0;end
                    end
                    if(next_state==FONT)begin
                        if(text_enable && ((code>=32 && code<128) || code>=160))begin
                            memory_address<=FONT_BASE+{9'b0,code,3'b0}+(scan>>1);font_odd<=scan[0];
                        end else begin glyph<=0;state<=PACK;pair_index<=0;end
                    end
                end
                PAGE:if(memory_ready)begin
                    characters<=memory_data;code<=memory_data[7:0];state<=GAP;next_state<=FONT;
                end
                FONT:if(memory_ready)begin glyph<=font_odd ? memory_data[15:8] : memory_data[7:0];state<=GAP;next_state<=PACK;pair_index<=0;end
                PACK:begin
                    if(pair_index!=3)pair_index<=pair_index+1'b1;
                    else if(glyph_index!=3)begin
                        glyph_index<=glyph_index+1'b1;pair_index<=0;state<=GAP;
                        if(!glyph_index[0])begin code<=characters[15:8];next_state<=FONT;end
                        else next_state<=PAGE;
                    end else if(group_index!=19)begin
                        group_index<=group_index+1'b1;glyph_index<=0;pair_index<=0;
                        state<=GAP;next_state<=graphics_enable ? GRAPH : PAGE;
                        memory_address<={5'b0,graphics_base[15:1]}+current_row*20+group_index+1'b1;
                    end else state<=COMPLETE;
                end
                COMPLETE:begin line_done<=line_sync[1];state<=IDLE;end
                default:state<=IDLE;
            endcase
        end
    end
endmodule
