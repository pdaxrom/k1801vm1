`timescale 1ns/1ps
// BK packed framebuffer: 512x256 mono or 256x256 four-colour PAL.
module bk_pal_video #(
    parameter COLOR = 0,
    parameter integer H_START = 718, V_START = 32
) (
    input wire clk, video_clk, reset,
    input wire [15:0] scroll,
    output wire dma_request,
    output reg [19:0] dma_address = 0,
    input wire dma_ready,
    input wire [15:0] dma_data,
    output wire [5:0] dac,
    output reg underrun = 0,
    input wire model11, color_mode, page,
    input wire [3:0] palette
);
    reg [2:0] srst_pipe = 7, vrst_pipe = 7 /* synthesis syn_preserve=1 */;
    always @(posedge clk or posedge reset)
        if(reset) srst_pipe <= 7; else srst_pipe <= {srst_pipe[1:0],1'b0};
    always @(posedge video_clk or posedge reset)
        if(reset) vrst_pipe <= 7; else vrst_pipe <= {vrst_pipe[1:0],1'b0};
    wire srst = srst_pipe[2], vrst = vrst_pipe[2];
    wire [11:0] x;
    wire [10:0] half_line;
    wire [9:0] line_number;
    wire line_start, frame_start, field, sync, burst, active_line, active, alternate;
    wire [7:0] row;
    // Preserve uJ11's PAL sync/burst. Position the BK framebuffer separately:
    // 50 clocks earlier = 8 mono pixels; four lines lower keeps the first
    // splash row inside the monitor's visible area.
    localparam integer FIRST = V_START, SECOND = V_START + 313;
    localparam [7:0] FIRST_ROW = FIRST, SECOND_ROW = SECOND;
    localparam [7:0] FIRST_PREFETCH = FIRST - 1, SECOND_PREFETCH = SECOND - 1;
    uj11_pal_timing #(.HEIGHT(256)) timing (
        .clk(video_clk), .reset(vrst), .x(x), .half_line(half_line),
        .line_number(line_number), .line_start(line_start), .frame_start(frame_start),
        .field(field), .sync(sync), .burst(burst), .active_line(),
        .active(), .row(), .v_alternate(alternate)
    );
    assign active_line = (line_number >= FIRST && line_number < FIRST + 256) ||
                         (line_number >= SECOND && line_number < SECOND + 256);
    assign row = line_number[7:0] - (field ? SECOND_ROW : FIRST_ROW);
    assign active = active_line && x >= H_START && x < H_START + 3200;
    wire [22:0] config_data = {model11,color_mode,page,palette,scroll};
    reg [22:0] held_config = {7'b0,16'o001330}, sent_config = {7'b0,16'o001330};
    reg config_toggle = 0, config_ack = 0;
    reg [1:0] ack_sync = 0, config_sync = 0 /* synthesis syn_preserve=1 */;
    reg [15:0] vscroll = 16'o001330;
    reg vmodel11 = 0, vcolor = COLOR, vpage = 0;
    reg [3:0] vpalette = 0;
    always @(posedge clk) begin
        if(srst) begin
            held_config <= {7'b0,16'o001330}; sent_config <= {7'b0,16'o001330};
            config_toggle <= 0; ack_sync <= 0;
        end else begin
            ack_sync <= {ack_sync[0],config_ack};
            if(config_toggle == ack_sync[1] && config_data != sent_config) begin
                held_config <= config_data; sent_config <= config_data; config_toggle <= !config_toggle;
            end
        end
    end
    // One row/bank request held until the completed DMA toggle returns.
    reg line_toggle = 0, fill_bank = 0, line_done = 0;
    reg [7:0] fill_row = 0;
    reg [1:0] done_sync = 0, line_sync = 0 /* synthesis syn_preserve=1 */;
    reg done_seen = 0;
    reg [1:0] bank_valid = 0;
    reg [7:0] tag0 = 0, tag1 = 0;
    reg display_valid = 0, display_bank = 0;
    wire prefetch = (line_number >= FIRST - 1 && line_number < FIRST + 255) ||
                    (line_number >= SECOND - 1 && line_number < SECOND + 255);
    wire [7:0] next_row = line_number[7:0] - (field ? SECOND_PREFETCH : FIRST_PREFETCH);
    wire [7:0] rotated_row = next_row + vscroll[7:0] - 8'o330;
    reg full_screen = 1, fill_model11 = 0, fill_page = 0;
    always @(posedge video_clk) begin
        if(vrst) begin
            config_sync <= 0; config_ack <= 0; vscroll <= 16'o001330;
            vmodel11 <= 0; vcolor <= COLOR; vpage <= 0; vpalette <= 0;
            done_sync <= 0; done_seen <= 0; line_toggle <= 0;
            fill_bank <= 0; fill_row <= 0; full_screen <= 1; fill_model11 <= 0; fill_page <= 0;
            bank_valid <= 0; tag0 <= 0; tag1 <= 0;
            display_valid <= 0; display_bank <= 0; underrun <= 0;
        end else begin
            config_sync <= {config_sync[0],config_toggle};
            done_sync <= {done_sync[0],line_done};
            if((half_line == 0 || half_line == 625) && x[10:0] == 0) begin
                bank_valid <= 0;
                if(config_sync[1] != config_ack) begin
                    {vmodel11,vcolor,vpage,vpalette,vscroll} <= held_config;
                    config_ack <= config_sync[1];
                end
            end
            if(done_sync[1] != done_seen) begin
                done_seen <= done_sync[1]; bank_valid[fill_bank] <= 1;
            end
            if(line_start) begin
                display_bank <= row[0];
                display_valid <= active_line && bank_valid[row[0]] &&
                    (row[0] ? tag1 == row : tag0 == row);
                if(active_line && !(bank_valid[row[0]] && (row[0] ? tag1 == row : tag0 == row))) underrun <= 1;
            end
            if(x == 32 && prefetch && line_toggle == done_sync[1]) begin
                fill_bank <= next_row[0]; fill_row <= rotated_row; full_screen <= vscroll[9];
                fill_model11 <= vmodel11; fill_page <= vpage;
                bank_valid[next_row[0]] <= 0;
                if(next_row[0]) tag1 <= next_row; else tag0 <= next_row;
                line_toggle <= !line_toggle;
            end
        end
    end
    reg [15:0] line_ram[0:63] /* synthesis syn_ramstyle = "block_ram" */;
    localparam IDLE = 0, READ = 1, GAP = 2, COMPLETE = 3;
    reg [1:0] dma_state = IDLE;
    reg dma_bank = 0;
    reg [4:0] word_index = 0;
    assign dma_request = dma_state == READ && !srst;
    always @(posedge clk) begin
        if(srst) begin line_sync <= 0; line_done <= 0; dma_state <= IDLE; word_index <= 0; dma_address <= 0; dma_bank <= 0; end
        else begin
            line_sync <= {line_sync[0],line_toggle};
            case(dma_state)
                IDLE: if(line_sync[1] != line_done) begin
                    dma_bank <= fill_bank;
                    dma_address <= {4'b0,fill_model11,
                        fill_model11 && fill_page,!(fill_model11 && fill_page),
                        full_screen ? fill_row[7:6] : 2'b11,fill_row[5:0],5'b0};
                    word_index <= 0; dma_state <= READ;
                end
                READ: if(dma_ready) begin
                    line_ram[{dma_bank,word_index}] <= dma_data;
                    dma_state <= GAP;
                end
                GAP: begin
                    if(word_index == 31) dma_state <= COMPLETE;
                    else begin word_index <= word_index + 1'b1; dma_address[4:0] <= word_index + 1'b1; dma_state <= READ; end
                end
                COMPLETE: begin line_done <= line_sync[1]; dma_state <= IDLE; end
            endcase
        end
    end
    // Fractional pixel clock: 4/25 (mono), 2/25 (colour) of 64 MHz.
    reg [5:0] pixel_phase = 0;
    reg [8:0] bit_index = 0;
    wire [5:0] phase_step = vcolor ? 6'd2 : 6'd4;
    wire advance = pixel_phase + phase_step >= 25;
    wire [8:0] upcoming_bit = bit_index + (vcolor ? 9'd2 : 9'd1);
    wire [4:0] read_word = active && advance ? upcoming_bit[8:4] : bit_index[8:4];
    reg [15:0] packed_word = 0;
    always @(posedge video_clk) begin
        packed_word <= line_ram[{display_bank,read_word}];
        if(vrst || !active) begin pixel_phase <= 0; bit_index <= 0; end
        else if(advance) begin pixel_phase <= pixel_phase + phase_step - 25; bit_index <= upcoming_bit; end
        else pixel_phase <= pixel_phase + phase_step;
    end
    wire [15:0] shifted = packed_word >> bit_index[3:0];
`include "rtl/bk_palette_index.vh"
    reg [3:0] color_index;
    always @* begin
        if(!vcolor) color_index = shifted[0] ? BK_WHITE_INDEX : 4'd0;
        else color_index = bk_palette_index(vmodel11 ? vpalette : 4'b0,shifted[1:0]);
    end
    bk_pal_encoder encoder (
        .clk(video_clk), .reset(vrst), .sync(sync), .burst(burst),
        .active(active && display_valid && (!vmodel11 || vscroll[9] || row < 64)),
        .v_alternate(alternate), .color_index(color_index), .dac(dac)
    );
endmodule
