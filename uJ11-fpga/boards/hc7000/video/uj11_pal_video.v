`timescale 1ns/1ps
// Optional HC7000 framebuffer. Only SERV sees this low-level register port;
// PDP-11 address decoding and byte-lane policy stay in storage firmware.
module uj11_pal_video #(parameter integer HEIGHT=200)(
    input wire clk, video_clk, reset,
    input wire reg_write,
    input wire [2:0] reg_address,
    input wire [15:0] reg_data,
    input wire [1:0] reg_lanes,
    output reg [15:0] reg_read,
    output wire dma_request,
    output reg [19:0] dma_address,
    input wire dma_ready,
    input wire [15:0] dma_data,
    output wire [5:0] dac
);
    // Both clocks get asynchronous assertion and synchronous reset release.
    reg [2:0] sys_reset_pipe=7, vid_reset_pipe=7;
    always @(posedge clk or posedge reset)
        if(reset)sys_reset_pipe<=7;else sys_reset_pipe<={sys_reset_pipe[1:0],1'b0};
    always @(posedge video_clk or posedge reset)
        if(reset)vid_reset_pipe<=7;else vid_reset_pipe<={vid_reset_pipe[1:0],1'b0};
    wire srst=sys_reset_pipe[2], vrst=vid_reset_pipe[2];
    reg [2:0] control;
    reg [20:0] base;
    reg [7:0] origin,palette_index;
    reg apply_toggle;
    reg config_ack,ack_seen;
    reg [1:0] ack_sync /* synthesis syn_preserve=1 */;
    wire config_busy=apply_toggle!=ack_seen;
    reg [19:0] active_base;
    reg active_enable;
    reg bad_config;
    reg [7:0] underruns;
    reg underrun_toggle;
    reg [1:0] underrun_sync /* synthesis syn_preserve=1 */;
    reg underrun_seen;
    reg [1:0] apply_sync /* synthesis syn_preserve=1 */;
    reg [2:0] vcontrol;
    reg [7:0] vorigin;
    wire [11:0] x;
    wire [10:0] half_line;
    wire [9:0] line_number;
    wire line_start,frame_start,field,sync,burst,active_line,active,v_alternate;
    wire [7:0] row;
    localparam integer FIRST=56-(HEIGHT-200)/2, SECOND=369-(HEIGHT-200)/2;
    uj11_pal_timing #(.HEIGHT(HEIGHT)) timing(.clk(video_clk),.reset(vrst),.x(x),.half_line(half_line),
        .line_number(line_number),.line_start(line_start),.frame_start(frame_start),.field(field),
        .sync(sync),.burst(burst),.active_line(active_line),.active(active),.row(row),.v_alternate(v_alternate));

    // One outstanding line mailbox. The producer holds row/bank until the
    // completion toggle returns. It never reuses a bank still being filled.
    reg line_toggle,fill_bank;
    reg [7:0] fill_row;
    reg line_done;
    reg [1:0] line_sync /* synthesis syn_preserve=1 */;
    reg [1:0] done_sync /* synthesis syn_preserve=1 */;
    reg done_seen;
    reg [1:0] bank_valid;
    reg [7:0] bank0_tag,bank1_tag;
    reg display_valid,display_bank;
    wire prefetch=(line_number>=FIRST-1 && line_number<FIRST+HEIGHT-1) ||
        (line_number>=SECOND-1 && line_number<SECOND+HEIGHT-1);
    wire [7:0] next_row=field ? line_number-(SECOND-1) : line_number-(FIRST-1);
    wire [8:0] rotated_row={1'b0,next_row}+{1'b0,vorigin};
    // At x=32 these two stages already contain the current line. They split
    // line subtraction, ring addition and wrap subtraction at 64 MHz.
    reg [7:0] next_row_pipe;
    reg [8:0] rotated_row_pipe;
    always @(posedge video_clk)begin
        if(vrst)begin next_row_pipe<=0;rotated_row_pipe<=0;end
        else begin next_row_pipe<=next_row;rotated_row_pipe<={1'b0,next_row_pipe}+{1'b0,vorigin};end
    end
    wire [8:0] ring_row=HEIGHT==240 ? rotated_row_pipe : rotated_row;
    always @(posedge video_clk) begin
        if(vrst) begin
            apply_sync<=0;config_ack<=0;vcontrol<=0;vorigin<=0;
            done_sync<=0;done_seen<=0;line_toggle<=0;fill_bank<=0;fill_row<=0;
            bank_valid<=0;bank0_tag<=0;bank1_tag<=0;
            display_valid<=0;display_bank<=0;underrun_toggle<=0;
        end else begin
            apply_sync<={apply_sync[0],apply_toggle};
            done_sync<={done_sync[0],line_done};
            if(frame_start && apply_sync[1]!=config_ack) begin
                // control/origin are held from request through ack (and the
                // source waits another two clocks to observe this ack).
                vcontrol<=control;vorigin<=origin;config_ack<=apply_sync[1];
            end
            if(done_sync[1]!=done_seen) begin
                done_seen<=done_sync[1];bank_valid[fill_bank]<=1;
            end
            if((half_line==0 || half_line==625) && x[10:0]==0)bank_valid<=0;
            if(line_start) begin
                display_bank<=row[0];
                display_valid<=active_line && bank_valid[row[0]] &&
                    (row[0] ? bank1_tag==row : bank0_tag==row);
                if(active_line && vcontrol[0] && !vcontrol[2] &&
                        !(bank_valid[row[0]] && (row[0] ? bank1_tag==row : bank0_tag==row)))
                    underrun_toggle<=!underrun_toggle;
            end
            if(x==32 && prefetch && vcontrol[0] && !vcontrol[2] &&
                    line_toggle==done_sync[1]) begin
                fill_bank<=next_row[0];
                fill_row<=ring_row>=HEIGHT ? ring_row-HEIGHT : ring_row[7:0];
                bank_valid[next_row[0]]<=0;
                if(next_row[0])bank1_tag<=next_row;else bank0_tag<=next_row;
                line_toggle<=!line_toggle;
            end
        end
    end

    localparam IDLE=0, READ=1, LOW=2, HIGH=3, COMPLETE=4;
    reg [2:0] state;
    reg [7:0] word_count;
    reg [15:0] word_data;
    reg [9:0] fill_address;
    reg palette_init;
    reg [7:0] init_index;
    wire palette_write=reg_write && reg_address==6 && reg_lanes[0] &&
        !active_enable && !config_busy && !palette_init && state==IDLE;
    wire ram_write=palette_init || palette_write || state==LOW || state==HIGH;
    wire [9:0] ram_waddr=palette_init ? 10'd640+{2'b0,init_index} :
        palette_write ? 10'd640+{2'b0,palette_index} : fill_address;
    wire [7:0] ram_wdata=palette_init ? init_index : palette_write ? reg_data[7:0] :
        state==LOW ? word_data[7:0] : word_data[15:8];
    assign dma_request=state==READ && !srst;
    always @(posedge clk) begin
        if(srst) begin
            control<=0;base<=0;origin<=0;palette_index<=0;apply_toggle<=0;ack_sync<=0;ack_seen<=0;
            active_base<=0;active_enable<=0;bad_config<=0;underruns<=0;
            underrun_sync<=0;underrun_seen<=0;
            line_sync<=0;line_done<=0;state<=IDLE;word_count<=0;word_data<=0;
            fill_address<=0;dma_address<=0;palette_init<=1;init_index<=0;
        end else begin
            ack_sync<={ack_sync[0],config_ack};
            underrun_sync<={underrun_sync[0],underrun_toggle};
            line_sync<={line_sync[0],line_toggle};
            if(ack_sync[1]!=ack_seen) begin
                ack_seen<=ack_sync[1];active_base<=base[20:1];active_enable<=control[0];
            end
            if(underrun_sync[1]!=underrun_seen) begin
                underrun_seen<=underrun_sync[1];
                if(underruns!=255)underruns<=underruns+1'b1;
            end
            if(palette_init) begin
                init_index<=init_index+1'b1;
                if(init_index==255)palette_init<=0;
            end
            if(reg_write) begin
                if(!config_busy) case(reg_address)
                    0: if(reg_lanes[0])control<=reg_data[2:0];
                    1: begin
                        if(reg_lanes[0])base[7:0]<=reg_data[7:0];
                        if(reg_lanes[1])base[15:8]<=reg_data[15:8];
                    end
                    2: if(reg_lanes[0])base[20:16]<=reg_data[4:0];
                    3: if(reg_lanes[0])origin<=reg_data[7:0];
                    4: if(reg_lanes[0] && reg_data[0]) begin
                        if(base[0] || base>2097152-320*HEIGHT || origin>=HEIGHT)bad_config<=1;
                        else apply_toggle<=!apply_toggle;
                    end
                    default: begin end
                endcase
                if(reg_address==5 && reg_lanes[0])palette_index<=reg_data[7:0];
                if(reg_address==7 && reg_lanes[0]) begin underruns<=0;bad_config<=0;end
            end
            case(state)
                IDLE: if(line_sync[1]!=line_done && !palette_init) begin
                    // Row is stable for two synchronizer clocks before this
                    // capture. 160 words/row = (row << 7) + (row << 5).
                    dma_address<=active_base+{5'b0,fill_row,7'b0}+{7'b0,fill_row,5'b0};
                    fill_address<=fill_bank ? 10'd320 : 10'd0;word_count<=0;state<=READ;
                end
                READ: if(dma_ready) begin word_data<=dma_data;state<=LOW;end
                LOW: begin fill_address<=fill_address+1'b1;state<=HIGH;end
                HIGH: begin
                    fill_address<=fill_address+1'b1;dma_address<=dma_address+1'b1;
                    word_count<=word_count+1'b1;state<=word_count==159 ? COMPLETE : READ;
                end
                COMPLETE: begin line_done<=line_sync[1];state<=IDLE;end
                default: state<=IDLE;
            endcase
        end
    end
    always @* begin
        case(reg_address)
            0: reg_read={13'b0,control};
            1: reg_read=base[15:0];
            2: reg_read={11'b0,base[20:16]};
            3: reg_read={8'b0,origin};
            4: reg_read={15'b0,config_busy};
            5: reg_read={8'b0,palette_index};
            6: reg_read=0; // Palette is write only; no competing system read port.
            7: reg_read={underruns,3'b0,palette_init,bad_config,|underruns,state!=IDLE,config_busy};
            default: reg_read=0;
        endcase
    end

    // One byte per ten clocks in both formats. At phase 0 fetch a packed
    // byte, 1 fetch its first colour, 3 its second. Phase 9 publishes the
    // first pixel; phase 4 publishes the previous byte's second pixel.
    reg [3:0] pixel_phase;
    reg [8:0] byte_index;
    reg [7:0] packed_byte,colour_a,colour_b,pixel_rgb;
    reg [9:0] ram_raddr;
    wire [7:0] ram_rdata;
    wire fetch_window=x>=758 && x<3958 && display_valid;
    wire ram_read=fetch_window && (pixel_phase==0 || pixel_phase==1 || pixel_phase==3);
    always @* begin
        if(pixel_phase==0)ram_raddr=(display_bank ? 10'd320 : 10'd0)+{1'b0,byte_index};
        else if(pixel_phase==1)ram_raddr=10'd640+(vcontrol[1] ? {2'b0,ram_rdata} : {6'b0,ram_rdata[3:0]});
        else ram_raddr=10'd640+{6'b0,packed_byte[7:4]};
    end
    uj11_pal_ram ram(.write_clk(clk),.write_enable(ram_write && !srst),.write_address(ram_waddr),
        .write_data(ram_wdata),.read_clk(video_clk),.read_enable(ram_read),.read_address(ram_raddr),.read_data(ram_rdata));
    always @(posedge video_clk) begin
        if(vrst) begin pixel_phase<=0;byte_index<=0;packed_byte<=0;colour_a<=0;colour_b<=0;pixel_rgb<=0;end
        else if(x==757) begin pixel_phase<=0;byte_index<=0;pixel_rgb<=0;end
        else if(x>=758 && x<3968) begin
            pixel_phase<=pixel_phase==9 ? 4'd0 : pixel_phase+1'b1;
            case(pixel_phase)
                1: begin packed_byte<=ram_rdata;byte_index<=byte_index+1'b1;end
                2: colour_a<=ram_rdata;
                4: begin colour_b<=ram_rdata;if(!vcontrol[1])pixel_rgb<=colour_b;end
                9: pixel_rgb<=colour_a;
                default: begin end
            endcase
        end
    end
    // Colour grid plus a fine luma pattern at the bottom for initial RCA
    // qualification without reserving or reading any operating-system RAM.
    reg [9:0] test_pixel;
    reg [2:0] test_div;
    always @(posedge video_clk) begin
        if(vrst || x<768) begin test_pixel<=0;test_div<=0;end
        else if(test_div==4) begin test_div<=0;test_pixel<=test_pixel+1'b1;end
        else test_div<=test_div+1'b1;
    end
    wire [7:0] test_rgb=row>=192 ? (test_pixel[0] ? 8'hff : 8'h00) :
        {test_pixel[8:6],row[6:4],test_pixel[5:4]};
    wire [7:0] rgb=vcontrol[2] ? test_rgb : display_valid ? pixel_rgb : 8'b0;
    uj11_pal_encoder encoder(.clk(video_clk),.reset(vrst),.sync(sync),.burst(burst),
        .active(active && vcontrol[0]),.v_alternate(v_alternate),.rgb(rgb),.dac(dac));
endmodule
