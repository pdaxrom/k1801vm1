`timescale 1ns/1ps
// One word per grant; round-robin when CPU and PAL request simultaneously.
module bk_sram_arbiter (
    input wire clk, reset,
    input wire cpu_request, cpu_write,
    input wire [19:0] cpu_address,
    input wire [15:0] cpu_data,
    input wire [1:0] cpu_lanes,
    output wire cpu_ready,
    input wire video_request,
    input wire [19:0] video_address,
    output wire video_ready,
    output wire request, write,
    output reg [19:0] address = 0,
    output reg [15:0] data = 0,
    output reg [1:0] lanes = 0,
    input wire ready
);
    reg [1:0] owner = 0;
    reg last_video = 1, writing = 0;
    wire choose_video = video_request && (!cpu_request || !last_video);
    assign request = owner == 1 || owner == 2;
    assign write = writing;
    assign cpu_ready = owner == 1 && ready;
    assign video_ready = owner == 2 && ready;
    always @(posedge clk) begin
        if(reset) begin owner <= 0; last_video <= 1; writing <= 0; address <= 0; data <= 0; lanes <= 0; end
        else case(owner)
            0: if(cpu_request || video_request) begin
                owner <= choose_video ? 2 : 1;
                address <= choose_video ? video_address : cpu_address;
                data <= choose_video ? 16'b0 : cpu_data;
                lanes <= choose_video ? 2'b11 : cpu_lanes;
                writing <= !choose_video && cpu_write;
                last_video <= choose_video;
            end
            1, 2: if(ready) owner <= 3;
            3: if(!ready) owner <= 0;
        endcase
    end
endmodule
