`timescale 1ns/1ps
// SD SPI card backed by a regular simulation file; supports verified CMD24 writes.
module sd_spi_model #(
    parameter HIGH_CAPACITY = 1, VERSION2 = 1,
    parameter IMAGE = "build/bk0010-sd.img"
) (
    input wire cs_n, sck, mosi,
    output reg miso = 1,
    output integer reads = 0
);
    reg [7:0] command[0:5], response[0:1023], sector[0:511];
    reg [7:0] incoming = 0, outgoing = 8'hff;
    integer bits = 0, command_bytes = 0, head = 0, tail = 0;
    integer image_fd, count, ignored, index;
    integer writes = 0, write_position = -1, write_fail_at = 0, corrupt_write_at = 0;
    integer card_sectors = 0, csd_size, corrupt_read_at = 0;
    reg write_pending = 0;
    reg [15:0] write_crc = 0, received_crc = 0, read_crc = 0;
    reg [7:0] csd[0:15];
    function [15:0] crc_byte(input [15:0] previous, input [7:0] value);
        reg [15:0] crc;
        integer bit_number;
        begin
            crc = previous ^ {value,8'b0};
            for(bit_number=0;bit_number<8;bit_number=bit_number+1)
                crc = crc[15] ? (crc << 1) ^ 16'h1021 : crc << 1;
            crc_byte = crc;
        end
    endfunction
    integer high_capacity = HIGH_CAPACITY, version2 = VERSION2;
    reg [1023:0] image_name;
    reg idle = 1, application = 0;
    reg [31:0] argument, lba;
    reg [7:0] received;
    task append(input [7:0] value);
        begin response[tail] = value; tail = tail + 1; end
    endtask
    task execute;
        begin
            argument = {command[1],command[2],command[3],command[4]};
            case(command[0] & 8'h3f)
                0: begin idle = 1; append(1); end
                8: begin
                    append(version2 ? 1 : 5);
                    if(version2) begin append(0);append(0);append(1);append(8'haa);end
                end
                55: begin application = 1; append(idle ? 1 : 0); end
                41: begin
                    if(application) begin idle = 0; application = 0; append(0); end
                    else append(4);
                end
                58: begin
                    append(idle ? 1 : 0); append(high_capacity ? 8'hc0 : 8'h80);
                    append(8'hff);append(8'h80);append(0);
                end
                9: begin
                    for(index=0;index<16;index=index+1) csd[index]=0;
                    if(high_capacity) begin
                        csd_size = card_sectors / 1024 - 1;
                        csd[0]=8'h40; csd[5]=9;
                        csd[7]=csd_size[21:16]; csd[8]=csd_size[15:8]; csd[9]=csd_size[7:0];
                    end else begin
                        csd_size = card_sectors / 32 - 1;
                        csd[5]=9; csd[6]=csd_size[11:10]; csd[7]=csd_size[9:2];
                        csd[8]={csd_size[1:0],6'b0}; csd[9]=1; csd[10]=8'h80;
                    end
                    append(0); append(8'hff); append(8'hfe);
                    for(index=0;index<16;index=index+1) append(csd[index]);
                    append(8'hff); append(8'hff);
                end
                24: begin
                    lba = high_capacity ? argument : argument >> 9;
                    if($test$plusargs("trace-sd")) begin
                        $display("CARD CMD24 LBA=%0d capacity=%0d SDHC=%0d",lba,card_sectors,high_capacity);
                        $fflush();
                    end
                    if(lba >= card_sectors || (!high_capacity && argument[8:0] != 0)) append(8'h20);
                    else begin append(0); write_pending=1; write_position=-1; write_crc=0; received_crc=0; end
                end
                16: append(argument == 512 ? 0 : 8'h40);
                17: begin
                    lba = high_capacity ? argument : argument >> 9;
                    ignored = $fseek(image_fd, lba * 512, 0);
                    count = $fread(sector, image_fd);
                    if(count != 512 || (!high_capacity && argument[8:0] != 0)) append(8'h20);
                    else begin
                        reads = reads + 1;
                        append(0);append(8'hff);append(8'hfe);
                        read_crc=0;
                        for(index = 0; index < 512; index = index + 1) begin
                            append(sector[index]); read_crc=crc_byte(read_crc,sector[index]);
                        end
                        append(read_crc[15:8]);append(read_crc[7:0] ^ (reads == corrupt_read_at ? 8'h01 : 8'h00));
                    end
                end
                default: append(4);
            endcase
        end
    endtask
    initial begin
        image_name = IMAGE;
        ignored = $value$plusargs("image=%s", image_name);
        ignored = $value$plusargs("sdhc=%d", high_capacity);
        ignored = $value$plusargs("version2=%d", version2);
        ignored = $value$plusargs("write-fail-at=%d", write_fail_at);
        ignored = $value$plusargs("corrupt-read-at=%d", corrupt_read_at);
        ignored = $value$plusargs("corrupt-write-at=%d", corrupt_write_at);
        image_fd = $fopen(image_name, "r+b");
        if(image_fd == 0) $fatal(1, "SD image unavailable: %s", image_name);
        if($fseek(image_fd,0,2)) $fatal(1,"Cannot determine SD capacity");
        card_sectors = $ftell(image_fd) / 512;
        if(card_sectors <= 0 || $fseek(image_fd,0,0)) $fatal(1,"Invalid SD simulation file");
    end
    always @(sck or posedge cs_n) begin
        if(cs_n) begin
            bits = 0; command_bytes = 0; incoming = 0;
            outgoing = 8'hff; miso = 1; head = 0; tail = 0; write_pending=0; write_position=-1;
        end else if(sck) begin
            incoming = {incoming[6:0], mosi};
            bits = bits + 1;
            if(bits == 8) begin
                received = incoming; bits = 0;
                if(write_pending) begin
                    if(write_position < 0) begin
                        if(received == 8'hfe) write_position=0;
                    end else if(write_position < 512) begin
                        sector[write_position]=received; write_crc=crc_byte(write_crc,received);
                        write_position=write_position+1;
                    end else if(write_position == 512) begin
                        received_crc[15:8]=received; write_position=513;
                    end else begin
                        received_crc[7:0]=received; write_pending=0;
                        writes=writes+1;
                        if(write_fail_at == writes) append(8'h0d);
                        else if(write_crc != received_crc) append(8'h0b);
                        else begin
                            if(corrupt_write_at == writes) sector[0]=sector[0]^1;
                            ignored = $fseek(image_fd,lba*512,0);
                            for(index=0;index<512;index=index+1) $fwrite(image_fd,"%c",sector[index]);
                            $fflush(image_fd);
                            append(8'h05);
                            // Busy does not finish on an arbitrary nonzero byte.
                            append(0); append(0); append(8'h01); append(8'h03); append(8'h7f); append(8'hff);
                        end
                    end
                end else if(command_bytes != 0 || received[7:6] == 2'b01) begin
                    command[command_bytes] = received; command_bytes = command_bytes + 1;
                    if(command_bytes == 6) begin command_bytes = 0; execute(); end
                end
            end
        end else begin
            if(bits == 0) begin
                if(head < tail) begin outgoing = response[head]; head = head + 1; end
                else outgoing = 8'hff;
                miso = outgoing[7];
            end else begin outgoing = {outgoing[6:0],1'b1}; miso = outgoing[7]; end
        end
    end
endmodule
