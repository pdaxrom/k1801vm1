`timescale 1ns/1ps
// Read-only SD SPI model backed by the generated regular image file.
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
                16: append(argument == 512 ? 0 : 8'h40);
                17: begin
                    lba = high_capacity ? argument : argument >> 9;
                    ignored = $fseek(image_fd, lba * 512, 0);
                    count = $fread(sector, image_fd);
                    if(count != 512 || (!high_capacity && argument[8:0] != 0)) append(8'h20);
                    else begin
                        reads = reads + 1;
                        append(0);append(8'hff);append(8'hfe);
                        for(index = 0; index < 512; index = index + 1) append(sector[index]);
                        append(8'hff);append(8'hff);
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
        image_fd = $fopen(image_name, "rb");
        if(image_fd == 0) $fatal(1, "SD image unavailable: %s", image_name);
    end
    always @(sck or posedge cs_n) begin
        if(cs_n) begin
            bits = 0; command_bytes = 0; incoming = 0;
            outgoing = 8'hff; miso = 1; head = 0; tail = 0;
        end else if(sck) begin
            incoming = {incoming[6:0], mosi};
            bits = bits + 1;
            if(bits == 8) begin
                received = incoming; bits = 0;
                if(command_bytes != 0 || received[7:6] == 2'b01) begin
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
