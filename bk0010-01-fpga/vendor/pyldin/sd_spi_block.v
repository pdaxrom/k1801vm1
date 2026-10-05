// Single-sector SPI SD host, SDSC/SDHC, mode 0, 512-byte buffer.
// All inputs and buffer accesses are in clk domain. Hold request until ready.
module sd_spi_block #(
    parameter EXTERNAL_INIT = 0,
    parameter INIT_DIV = 29, // 400 kHz at 24 MHz
    parameter RUN_DIV = 1   // 6 MHz at 24 MHz
) (
    input wire clk, reset,
    input wire request, write,
    input wire [31:0] lba,
    output wire ready,
    output reg initialized, done, error,
    input wire [8:0] buffer_address,
    input wire buffer_write,
    input wire [7:0] buffer_data_in,
    output wire [7:0] buffer_data_out,
    output reg sd_cs,
    input wire init_valid,block_mode,adopt_boot,
    output reg byte_start,output reg[7:0]byte_tx,output wire[7:0]divider,
    input wire byte_busy,byte_done,input wire[7:0]byte_rx
);
    localparam POWER=0, IDLE_CLOCK=1, INIT0=2, INIT8=3, CHECK8=4,
        INIT55=5, INIT41=6, CHECK41=7, INIT58=8, CHECK58=9,
        INIT16=10, CHECK16=11, IDLE=12, READ_CMD=13, READ_TOKEN=14,
        READ_DATA=15, READ_CRC1=16, READ_CRC2=17, WRITE_CMD=18,
        WRITE_TOKEN=19, WRITE_DATA=20, WRITE_CRC1=21, WRITE_CRC2=22,
        WRITE_RESPONSE=23, WRITE_BUSY=24, FINISH=25, FINISH_CLOCK=26,
        CMD_SEND=27, CMD_POLL=28, CMD_EXTRA=29, CMD_END=30,
        CMD_RELEASE=31, CHECK0=32, CHECK55=33, FAILED=34, WRITE_FETCH=35;
    reg [5:0] state, return_state;
    (* syn_ramstyle = "block_ram" *) reg [7:0] buffer [0:511];
    reg[7:0]buffer_cpu_q,buffer_spi_q;
    reg [15:0] power_count, timeout_count, crc, received_crc;
    reg [9:0] init_attempt;
    reg [8:0] index;
    reg [3:0] clocks;
    reg [47:0] command;
    reg [2:0] command_index, extra_count;
    reg [7:0] response;
    reg [31:0] extra, block_lba;
    reg keep_selected, v2, block_addressing, waiting;
    assign divider = initialized ? RUN_DIV : INIT_DIV;
    assign ready = initialized && state == IDLE;
    assign buffer_data_out = buffer_cpu_q;
    wire receive_store=state==READ_DATA&&waiting&&byte_done;
    wire store=receive_store||(buffer_write&&ready);
    wire[8:0]store_address=receive_store?index:buffer_address;
    wire[7:0]store_data=receive_store?byte_rx:buffer_data_in;
    always @(posedge clk)begin
        if(store)buffer[store_address]<=store_data;
        buffer_cpu_q<=buffer[buffer_address];
        buffer_spi_q<=buffer[index];
    end

    function [15:0] crc16;
        input [15:0] old;
        input [7:0] data;
        reg [15:0] c;
        integer i;
        begin
            c = old ^ {data,8'b0};
            for (i=0;i<8;i=i+1) c = c[15] ? (c << 1) ^ 16'h1021 : c << 1;
            crc16 = c;
        end
    endfunction
    task send_byte;
        input [7:0] data;
        begin byte_tx <= data; byte_start <= 1; waiting <= 1; end
    endtask
    task send_command;
        input [5:0] op;
        input [31:0] arg;
        input [7:0] check;
        input [2:0] trailing;
        input keep;
        input [5:0] next_state;
        begin
            command <= {2'b01,op,arg,check}; command_index <= 0;
            extra_count <= trailing; extra <= 0; keep_selected <= keep;
            return_state <= next_state; timeout_count <= 0;
            sd_cs <= 0; state <= CMD_SEND;
        end
    endtask
    task fail;
        begin error <= 1; sd_cs <= 1; state <= initialized?FINISH_CLOCK:FAILED; end
    endtask

    always @(posedge clk) begin
        byte_start <= 0; done <= 0;
        if (byte_done) waiting <= 0;
        if (reset) begin
            state <= POWER; power_count <= 0; initialized <= 0; done <= 0;
            error <= 0; sd_cs <= 1; waiting <= 0; byte_start <= 0;
            clocks <= 0; index <= 0; init_attempt <= 0;
            v2 <= 0; block_addressing <= 0; crc <= 0; received_crc <= 0;
            timeout_count <= 0; response <= 0; extra <= 0; block_lba <= 0;
            command <= 0; command_index <= 0; extra_count <= 0;
            keep_selected <= 0; return_state <= 0; byte_tx <= 8'hff;
        end else case (state)
            POWER: if(EXTERNAL_INIT&&!init_valid)begin end
                   else if(EXTERNAL_INIT&&adopt_boot)begin initialized<=1;block_addressing<=block_mode;state<=IDLE;end
                   else if (power_count == 16'd48000) state <= IDLE_CLOCK;
                   else power_count <= power_count + 1'b1;
            IDLE_CLOCK: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if (clocks == 9) state <= INIT0;
                    else clocks <= clocks + 1'b1;
                end
            INIT0: send_command(0,0,8'h95,0,0,CHECK0);
            CHECK0: if (response != 1) fail(); else state <= INIT8;
            INIT8: send_command(8,32'h1aa,8'h87,4,0,CHECK8);
            CHECK8: begin
                if (response == 1 && extra[11:0] == 12'h1aa) begin
                    v2 <= 1; state <= INIT55;
                end else if (response == 5) begin v2 <= 0; state <= INIT55; end
                else fail();
            end
            INIT55: send_command(55,0,8'h01,0,0,CHECK55);
            CHECK55: if (response > 1) fail(); else state <= INIT41;
            INIT41: send_command(41,v2 ? 32'h40000000 : 0,8'h01,0,0,CHECK41);
            CHECK41: if (response == 0) state <= INIT58;
                else if (response != 1 || init_attempt == 1023) fail();
                else begin init_attempt <= init_attempt + 1'b1; state <= INIT55; end
            INIT58: send_command(58,0,8'h01,4,0,CHECK58);
            CHECK58: if (response != 0 || !extra[31] || extra[23:15] == 0) fail();
                else begin
                    block_addressing <= v2 && extra[30];
                    if (v2 && extra[30]) begin initialized <= 1; state <= IDLE; end
                    else state <= INIT16;
                end
            INIT16: send_command(16,512,8'h01,0,0,CHECK16);
            CHECK16: if (response != 0) fail();
                     else begin initialized <= 1; state <= IDLE; end
            IDLE: begin
              error<=0;
              if (request) begin
                // SDSC byte addresses must fit in 32 bits.
                if (!block_addressing && lba[31:23] != 0) fail();
                else begin
                    block_lba <= block_addressing ? lba : lba << 9;
                    state <= write ? WRITE_CMD : READ_CMD;
                end
            end
              end
            READ_CMD: send_command(17,block_lba,8'h01,0,1,READ_TOKEN);
            READ_TOKEN: if (response != 0) fail();
                else if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if (byte_rx == 8'hfe) begin index <= 0; crc <= 0; state <= READ_DATA; end
                    else if (byte_rx != 8'hff || timeout_count == 16'hffff) fail();
                    else timeout_count <= timeout_count + 1'b1;
                end
            READ_DATA: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    crc <= crc16(crc,byte_rx);
                    if (index == 511) state <= READ_CRC1;
                    else index <= index + 1'b1;
                end
            READ_CRC1: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin received_crc[15:8] <= byte_rx; state <= READ_CRC2; end
            READ_CRC2: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if ({received_crc[15:8],byte_rx} != crc) fail();
                    else state <= FINISH;
                end
            WRITE_CMD: send_command(24,block_lba,8'h01,0,1,WRITE_TOKEN);
            WRITE_TOKEN: if (response != 0) fail();
                else if (!waiting) send_byte(8'hfe);
                else if (byte_done) begin index <= 0; crc <= 0; state <= WRITE_FETCH; end
            WRITE_FETCH: state<=WRITE_DATA;
            WRITE_DATA: if (!waiting) send_byte(buffer_spi_q);
                else if (byte_done) begin
                    crc <= crc16(crc,byte_tx);
                    if (index == 511) state <= WRITE_CRC1;
                    else begin index <= index + 1'b1; state<=WRITE_FETCH; end
                end
            WRITE_CRC1: if (!waiting) send_byte(crc[15:8]);
                else if (byte_done) state <= WRITE_CRC2;
            WRITE_CRC2: if (!waiting) send_byte(crc[7:0]);
                else if (byte_done) begin state <= WRITE_RESPONSE; timeout_count <= 0; end
            WRITE_RESPONSE: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if (byte_rx[4:0] == 5'b00101) begin state <= WRITE_BUSY; timeout_count <= 0; end
                    else if (byte_rx != 8'hff || timeout_count == 16'hffff) fail();
                    else timeout_count <= timeout_count + 1'b1;
                end
            WRITE_BUSY: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if (byte_rx == 8'hff) state <= FINISH;
                    else if (timeout_count == 16'hffff) fail();
                    else timeout_count <= timeout_count + 1'b1;
                end
            FINISH: begin sd_cs <= 1; state <= FINISH_CLOCK; end
            FINISH_CLOCK: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin done <= 1; state <= IDLE; end
            CMD_SEND: if (!waiting) send_byte(command[47:40]);
                else if (byte_done) begin
                    command <= {command[39:0],8'hff};
                    if (command_index == 5) state <= CMD_POLL;
                    else command_index <= command_index + 1'b1;
                end
            CMD_POLL: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    if (!byte_rx[7]) begin
                        response <= byte_rx;
                        // Illegal CMD8 on SDSC has no R7 trailing bytes.
                        state <= extra_count != 0 && byte_rx <= 1 ? CMD_EXTRA : CMD_END;
                    end else if (timeout_count == 15) fail();
                    else timeout_count <= timeout_count + 1'b1;
                end
            CMD_EXTRA: if (!waiting) send_byte(8'hff);
                else if (byte_done) begin
                    extra <= {extra[23:0],byte_rx}; extra_count <= extra_count - 1'b1;
                    if (extra_count == 1) state <= CMD_END;
                end
            CMD_END: if (keep_selected) begin state <= return_state; timeout_count <= 0; end
                else begin sd_cs <= 1; state <= CMD_RELEASE; end
            CMD_RELEASE: if (!waiting) send_byte(8'hff);
                else if (byte_done) state <= return_state;
            FAILED: ;
            default: fail();
        endcase
    end
endmodule
