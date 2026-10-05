`timescale 1ns/1ps
// One cached native MFM track. The backing store is ten absolute SD sectors;
// no filesystem or CPU-PC interception participates in runtime disk transfers.
module bk_fdd_track (
    input wire clk, power_on, quiesce, writing,
    input wire selected,
    input wire [1:0] drive,
    input wire [6:0] track,
    input wire side,
    input wire [3:0] present, readonly,
    output wire valid, idle,
    output reg error = 0,
    input wire [11:0] position,
    output wire [16:0] word_data,
    input wire word_write,
    input wire [11:0] word_address,
    input wire [16:0] word_value,
    output reg sector_request = 0, sector_write = 0,
    output reg [1:0] sector_drive = 0,
    output wire [10:0] sector_offset,
    input wire sector_ready, sector_done, sector_error,
    output reg [8:0] buffer_address = 0,
    output reg buffer_write = 0,
    output reg [7:0] buffer_data = 0,
    input wire [7:0] buffer_read,
    input wire [15:0] next_crc,
    output wire [15:0] crc_previous,crc_value
);
    localparam IDLE=0, CLEAR=1, READ_START=2, READ_WAIT=3, HEADER=4, PREFIX=5,
        HIGH_ADDR=6, HIGH_WAIT=7, HIGH_BYTE=8, LOW_WAIT=9, LOW_BYTE=10,
        PAYLOAD=11, DATA_CRC=12, NEXT_SECTOR=13,
        SCAN_ADDR=14, SCAN_WAIT=15, SCAN_WORD=16, BUFFER_HIGH=17, BUFFER_LOW=18,
        WRITE_START=19, WRITE_WAIT=20, FINISH=21, FAILED=22;
    reg [4:0] state = IDLE;
    reg cached = 0, dirty = 0;
    reg [1:0] cached_drive = 0;
    reg [6:0] cached_track = 0;
    reg cached_side = 0;
    reg [3:0] sector = 1, header_sector = 0;
    reg [11:0] index = 0;
    reg [8:0] count = 0;
    reg [2:0] field = 0;
    reg [1:0] frame = 0;
    reg header_ok = 0;
    reg [7:0] high_byte = 0;
    reg [15:0] crc = 16'hffff, pending_word = 0;
    (* syn_ramstyle = "block_ram" *) reg [16:0] words[0:4095];
    reg [16:0] read_word;
    reg store = 0;
    reg [11:0] store_address = 0;
    reg [16:0] store_value = 0;
    // Construction and scanning share one stream address incrementer.
    wire [11:0] index_step = state == HEADER && field == 4 ? 12'hff6 :
                             state == PREFIX && count == 5 ? 12'd17 :
                             state == NEXT_SECTOR ? 12'd31 : 12'd1;
    wire [11:0] next_index = index + index_step;
    wire owner_matches = cached && cached_drive == drive && cached_track == track && cached_side == side;
    assign valid = state == IDLE && selected && present[drive] && owner_matches && !error;
    assign idle = state == IDLE && !dirty && !word_write && !sector_request;
    wire [11:0] read_address = state == IDLE ? position : index;
    assign word_data = read_word;
    assign sector_offset = {cached_track,4'b0} + {2'b0,cached_track,2'b0} +
                           (cached_side ? 11'd10 : 11'd0) + {7'b0,sector} - 1'b1;
    always @(posedge clk) begin
        read_word <= words[read_address];
        if(store) words[store_address] <= store_value;
        else if(valid && word_write && !readonly[drive]) words[word_address] <= word_value;
    end
    wire [15:0] header_value = field == 0 ? 16'ha1a1 : field == 1 ? 16'ha1fe :
                                 field == 2 ? {1'b0,cached_track,7'b0,cached_side} : {4'b0,sector,8'd2};
    assign crc_value = state == HEADER ? header_value :
                           state == PREFIX ? (field == 0 ? 16'ha1a1 : 16'ha1fb) :
                           state == PAYLOAD ? pending_word : read_word[15:0];
    assign crc_previous = state == SCAN_WORD && read_word[16] ? 16'hffff : crc;
    task put;
        input [11:0] address;
        input marker;
        input [15:0] data;
        begin store <= 1; store_address <= address; store_value <= {marker,data}; end
    endtask
    task scan_next;
        begin
            if(index == 3124) state <= FINISH;
            else begin index <= next_index; state <= SCAN_ADDR; end
        end
    endtask
    always @(posedge clk) begin
        store <= 0; buffer_write <= 0;
        if(power_on) begin
            state <= IDLE; cached <= 0; dirty <= 0; error <= 0;
            sector_request <= 0; sector_write <= 0; sector_drive <= 0;
            index <= 0; count <= 0; field <= 0; frame <= 0;
            sector <= 1; header_sector <= 0; header_ok <= 0; crc <= 16'hffff;
            buffer_address <= 0; buffer_data <= 0; high_byte <= 0; pending_word <= 0;
            cached_drive <= 0; cached_track <= 0; cached_side <= 0;
        end else case(state)
            IDLE: begin
                if(valid && word_write && !readonly[drive]) dirty <= 1;
                if(!writing && dirty && !word_write) begin
                    sector_drive <= cached_drive; sector_write <= 1;
                    index <= 0; frame <= 0; header_sector <= 0; state <= SCAN_ADDR;
                end else if(!quiesce && selected && present[drive] && !owner_matches && !writing) begin
                    cached <= 0; cached_drive <= drive; cached_track <= track; cached_side <= side;
                    sector_drive <= drive; sector <= 1; sector_write <= 0;
                    index <= 0; error <= 0; state <= CLEAR;
                end
            end
            CLEAR: begin
                put(index,0,16'h4e4e);
                if(index == 3124) begin index <= 22; state <= READ_START; end
                else index <= next_index;
            end
            READ_START: if(sector_ready) begin sector_request <= 1; state <= READ_WAIT; end
            READ_WAIT: if(sector_done) begin
                sector_request <= 0;
                if(sector_error) begin error <= 1; state <= IDLE; end
                else begin field <= 0; crc <= 16'hffff; state <= HEADER; end
            end
            HEADER: begin
                put(index,field == 0,field == 4 ? crc : header_value);
                index <= next_index;
                crc <= next_crc;
                if(field == 4) begin
                    // Six sync zero words before each address/data marker.
                    field <= 0; count <= 0; state <= PREFIX;
                end else field <= field + 1'b1;
            end
            PREFIX: if(count < 12) begin
                put(index,0,0); count <= count + 1'b1;
                index <= next_index;
                if(count == 11) begin field <= 0; crc <= 16'hffff; end
            end else begin
                put(index,field == 0,field == 0 ? 16'ha1a1 : 16'ha1fb);
                index <= next_index;
                crc <= next_crc;
                if(field == 1) begin count <= 0; buffer_address <= 0; state <= HIGH_ADDR; end
                else field <= 1;
            end
            HIGH_ADDR: state <= HIGH_WAIT;
            HIGH_WAIT: state <= HIGH_BYTE;
            HIGH_BYTE: begin high_byte <= buffer_read; buffer_address <= buffer_address + 1'b1; state <= LOW_WAIT; end
            LOW_WAIT: state <= LOW_BYTE;
            LOW_BYTE: begin pending_word <= {high_byte,buffer_read}; state <= PAYLOAD; end
            PAYLOAD: begin
                put(index,0,pending_word); index <= next_index; crc <= next_crc;
                if(count == 255) state <= DATA_CRC;
                else begin count <= count + 1'b1; buffer_address <= buffer_address + 1'b1; state <= HIGH_ADDR; end
            end
            DATA_CRC: begin put(index,0,crc); state <= NEXT_SECTOR; end
            NEXT_SECTOR: begin
                if(sector == 10) begin cached <= 1; dirty <= 0; state <= IDLE; end
                else begin sector <= sector + 1'b1; index <= next_index; state <= READ_START; end
            end
            SCAN_ADDR: state <= SCAN_WAIT;
            SCAN_WAIT: state <= SCAN_WORD;
            SCAN_WORD: begin
                if(read_word[16]) begin
                    crc <= next_crc;
                    frame <= 0; field <= 1; header_ok <= 0;
                end else if(field == 1) begin
                    crc <= next_crc; field <= 2;
                    if(read_word[15:0] == 16'ha1fe) begin frame <= 1; header_sector <= 0; end
                    else if(read_word[15:0] == 16'ha1fb && header_sector != 0) begin
                        frame <= 2; count <= 0; buffer_address <= 0;
                    end else begin frame <= 0; field <= 0; end
                end else if(frame == 1) begin
                    crc <= next_crc; field <= field + 1'b1;
                    if(field == 2) header_ok <= read_word[15:0] == {1'b0,cached_track,7'b0,cached_side};
                    else if(field == 3) begin
                        header_ok <= header_ok && read_word[7:0] == 2 && read_word[15:8] >= 1 && read_word[15:8] <= 10;
                        header_sector <= read_word[11:8];
                    end else begin
                        if(!header_ok || read_word[15:0] != crc) header_sector <= 0;
                        frame <= 0; field <= 0;
                    end
                end else if(frame == 2) begin
                    if(count == 256) begin
                        if(read_word[15:0] == crc && !readonly[cached_drive]) begin
                            sector <= header_sector; state <= WRITE_START;
                        end else scan_next();
                        frame <= 0; field <= 0;
                    end else begin
                        pending_word <= read_word[15:0]; crc <= next_crc;
                        buffer_address <= {count[7:0],1'b0}; state <= BUFFER_HIGH;
                    end
                end
                if(!(frame == 2 && !read_word[16] && field != 1)) scan_next();
            end
            BUFFER_HIGH: begin buffer_data <= pending_word[15:8]; buffer_write <= 1; state <= BUFFER_LOW; end
            BUFFER_LOW: begin
                buffer_address <= buffer_address + 1'b1; buffer_data <= pending_word[7:0]; buffer_write <= 1;
                count <= count + 1'b1; state <= SCAN_ADDR; index <= next_index;
            end
            WRITE_START: if(sector_ready) begin sector_request <= 1; state <= WRITE_WAIT; end
            WRITE_WAIT: if(sector_done) begin
                sector_request <= 0;
                if(sector_error) begin error <= 1; state <= FAILED; end
                else begin index <= next_index; state <= SCAN_ADDR; end
            end
            FINISH: begin dirty <= 0; error <= 0; state <= IDLE; end
            FAILED: if(quiesce && sector_ready) begin
                // Keep the complete dirty track after a failed SD write. RESET
                // requests another drain; it cannot discard the cached data.
                index <= 0; frame <= 0; header_sector <= 0; state <= SCAN_ADDR;
            end
            default: state <= IDLE;
        endcase
    end
endmodule
