`timescale 1ns/1ps
// The original 4 KiB I/O window selects a separate page or character RAM.
// Only the documented 25x80 page bytes and 192x10 glyph bytes are stored.
// Native row/code strides stay at the interface; packing is private to EBR.
// Four true-dual-port 1024x9 blocks: byte port A for CPU, byte port B for video.
// Two successive video reads form the same little-endian word as SRAM.
module uj11_terak_display_ram(
    input wire clk,reset,power_on,
    output reg initialized=0,
    input wire cpu_request,cpu_write,cpu_font,
    input wire [11:0] cpu_address,
    input wire [15:0] cpu_data,
    output reg cpu_ready,
    output wire [15:0] cpu_read_data,
    input wire video_request,video_font,
    input wire [11:0] video_address,
    output reg video_ready,
    output reg [15:0] video_data
);
    function automatic [10:0] packed_address(input [11:0] address,input font);
        reg [7:0] character;
        begin
            character={1'b0,address[10:4]}-8'd32+(address[11] ? 8'd96 : 8'd0);
            packed_address=font ? {character,3'b0}+{2'b0,character,1'b0}+{7'b0,address[3:0]} :
                {address[11:7],6'b0}+{2'b0,address[11:7],4'b0}+{4'b0,address[6:0]};
        end
    endfunction
    function automatic valid_address(input [11:0] address,input font);
        valid_address=font ? (|address[10:9]) && address[3:0]<10 : address[11:7]<25 && address[6:0]<80;
    endfunction
    wire [10:0] cpu_packed=packed_address(cpu_address,cpu_font);
    wire [10:0] video_packed=packed_address(video_address,video_font);
    wire cpu_valid=valid_address(cpu_address,cpu_font);
    wire video_valid=valid_address(video_address,video_font);
    reg [11:0] clear_address=0;
    always @(posedge clk)begin
        if(power_on)begin initialized<=0;clear_address<=0;end
        else if(!initialized)begin
            if(clear_address==4095)initialized<=1;
            else clear_address<=clear_address+1'b1;
        end
    end
    wire cpu_access=initialized && !reset && !power_on && cpu_request && !cpu_ready && cpu_valid;
    wire a_enable=!power_on && (!initialized || cpu_access);
    wire a_write=!initialized || cpu_write;
    wire [1:0] a_bank=initialized ? {cpu_font,cpu_packed[10]} : clear_address[11:10];
    wire [9:0] a_address=initialized ? cpu_packed[9:0] : clear_address[9:0];
    wire [7:0] a_data=initialized ? (cpu_address[0] ? cpu_data[15:8] : cpu_data[7:0]) : 8'b0;
    wire [7:0] a_read[0:3],b_read[0:3];
    reg [1:0] cpu_bank;
    reg cpu_read_valid;
    always @(posedge clk)begin
        if(reset || power_on || !initialized)begin cpu_ready<=0;cpu_bank<=0;cpu_read_valid<=0;end
        else if(!cpu_request)cpu_ready<=0;
        else if(!cpu_ready)begin
            cpu_ready<=1;cpu_bank<={cpu_font,cpu_packed[10]};cpu_read_valid<=cpu_valid;
        end
    end
    // Byte-only guest accesses: duplicate the byte for either CPU lane.
    wire [7:0] cpu_byte=cpu_read_valid ? a_read[cpu_bank] : 8'b0;
    assign cpu_read_data={cpu_byte,cpu_byte};
    reg [1:0] video_phase,video_bank;
    reg [10:0] video_saved;
    reg video_read_valid;
    reg [7:0] video_low;
    wire video_first=video_request && !video_ready && video_phase==0;
    wire b_enable=initialized && !reset && !power_on &&
        ((video_first && video_valid) || (video_phase==1 && video_read_valid));
    wire [1:0] b_bank=video_phase==0 ? {video_font,video_packed[10]} : video_bank;
    wire [9:0] b_address=video_phase==0 ? video_packed[9:0] : video_saved[9:0]+1'b1;
    always @(posedge clk)begin
        if(reset || power_on || !initialized)begin
            video_phase<=0;video_ready<=0;video_bank<=0;video_saved<=0;
            video_read_valid<=0;video_low<=0;video_data<=0;
        end else if(!video_request)begin video_phase<=0;video_ready<=0;end
        else if(!video_ready)case(video_phase)
            0:begin
                video_saved<=video_packed;video_bank<={video_font,video_packed[10]};
                video_read_valid<=video_valid;video_phase<=1;
            end
            1:begin video_low<=video_read_valid ? b_read[video_bank] : 8'b0;video_phase<=2;end
            2:begin
                video_data<={video_read_valid ? b_read[video_bank] : 8'b0,video_low};
                video_ready<=1;video_phase<=3;
            end
            default:begin end
        endcase
    end
    genvar bank;
    generate for(bank=0;bank<4;bank=bank+1)begin: banks
        wire ae=a_enable && a_bank==bank;
        wire be=b_enable && b_bank==bank;
`ifdef SYNTHESIS
`define UJ11_TERAK_DISPLAY_VENDOR
`elsif UJ11_VIDEO_VENDOR_RAM
`define UJ11_TERAK_DISPLAY_VENDOR
`endif
`ifdef UJ11_TERAK_DISPLAY_VENDOR
        DP8KC #(.DATA_WIDTH_A(9),.DATA_WIDTH_B(9),.REGMODE_A("NOREG"),.REGMODE_B("NOREG"),
            .CSDECODE_A("0b000"),.CSDECODE_B("0b000"),.WRITEMODE_A("NORMAL"),.WRITEMODE_B("NORMAL"),
            .GSR("DISABLED"),.RESETMODE("SYNC"),.ASYNC_RESET_RELEASE("SYNC")) ebr(
            .DIA0(a_data[0]),
            .DIB0(1'b0),
            .DOA0(a_read[bank][0]),
            .DOB0(b_read[bank][0]),
            .DIA1(a_data[1]),
            .DIB1(1'b0),
            .DOA1(a_read[bank][1]),
            .DOB1(b_read[bank][1]),
            .DIA2(a_data[2]),
            .DIB2(1'b0),
            .DOA2(a_read[bank][2]),
            .DOB2(b_read[bank][2]),
            .DIA3(a_data[3]),
            .DIB3(1'b0),
            .DOA3(a_read[bank][3]),
            .DOB3(b_read[bank][3]),
            .DIA4(a_data[4]),
            .DIB4(1'b0),
            .DOA4(a_read[bank][4]),
            .DOB4(b_read[bank][4]),
            .DIA5(a_data[5]),
            .DIB5(1'b0),
            .DOA5(a_read[bank][5]),
            .DOB5(b_read[bank][5]),
            .DIA6(a_data[6]),
            .DIB6(1'b0),
            .DOA6(a_read[bank][6]),
            .DOB6(b_read[bank][6]),
            .DIA7(a_data[7]),
            .DIB7(1'b0),
            .DOA7(a_read[bank][7]),
            .DOB7(b_read[bank][7]),
            .DIA8(1'b0),
            .DIB8(1'b0),
            .DOA8(),
            .DOB8(),
            .ADA0(1'b1),
            .ADB0(1'b0),
            .ADA1(1'b0),
            .ADB1(1'b0),
            .ADA2(1'b0),
            .ADB2(1'b0),
            .ADA3(a_address[0]),
            .ADB3(b_address[0]),
            .ADA4(a_address[1]),
            .ADB4(b_address[1]),
            .ADA5(a_address[2]),
            .ADB5(b_address[2]),
            .ADA6(a_address[3]),
            .ADB6(b_address[3]),
            .ADA7(a_address[4]),
            .ADB7(b_address[4]),
            .ADA8(a_address[5]),
            .ADB8(b_address[5]),
            .ADA9(a_address[6]),
            .ADB9(b_address[6]),
            .ADA10(a_address[7]),
            .ADB10(b_address[7]),
            .ADA11(a_address[8]),
            .ADB11(b_address[8]),
            .ADA12(a_address[9]),
            .ADB12(b_address[9]),
            .CLKA(clk),
            .CLKB(clk),
            .CEA(ae),
            .CEB(be),
            .OCEA(1'b1),
            .OCEB(1'b1),
            .WEA(a_write),
            .WEB(1'b0),
            .RSTA(1'b0),
            .RSTB(1'b0),
            .CSA0(1'b0),
            .CSA1(1'b0),
            .CSA2(1'b0),
            .CSB0(1'b0),
            .CSB1(1'b0),
            .CSB2(1'b0)
        );
`else
        reg [7:0] bytes[0:1023];
        reg [7:0] a_value,b_value;
        always @(posedge clk)begin
            if(ae)begin
                if(a_write)bytes[a_address]<=a_data;
                else a_value<=bytes[a_address];
            end
            if(be)b_value<=bytes[b_address];
        end
        assign a_read[bank]=a_value,b_read[bank]=b_value;
`endif
`undef UJ11_TERAK_DISPLAY_VENDOR
    end endgenerate
endmodule
