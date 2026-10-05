`timescale 1ns/1ps
// One outstanding Wishbone transfer. Bundled payload is held until response.
// The return data is held through the corresponding synchronized toggle.
module bk_wb_cdc (
    input wire cpu_clk, sys_clk, reset,
    input wire cpu_cycle, cpu_strobe, cpu_write,
    input wire [15:0] cpu_address, cpu_data,
    input wire [1:0] cpu_lanes,
    output reg cpu_ack = 0,
    output reg [15:0] cpu_read = 0,
    output wire cpu_busy,
    output wire request, write,
    output wire [15:0] address, write_data,
    output wire [1:0] lanes,
    input wire ready,
    input wire [15:0] read_data,
    input wire [15:0] cpu_map_register,
    output wire [15:0] map_register
);
    reg [2:0] cpu_reset_pipe = 7, sys_reset_pipe = 7 /* synthesis syn_preserve=1 */;
    always @(posedge cpu_clk or posedge reset)
        if(reset) cpu_reset_pipe <= 7; else cpu_reset_pipe <= {cpu_reset_pipe[1:0],1'b0};
    always @(posedge sys_clk or posedge reset)
        if(reset) sys_reset_pipe <= 7; else sys_reset_pipe <= {sys_reset_pipe[1:0],1'b0};
    wire cpu_reset = cpu_reset_pipe[2], sys_reset = sys_reset_pipe[2];
    reg request_toggle = 0, response_toggle = 0;
    reg [15:0] held_address = 0, held_data = 0, held_read = 0;
    reg [15:0] held_map = 0;
    reg [1:0] held_lanes = 0;
    reg held_write = 0;
    reg valid = 0, cancelled = 0;
    reg [1:0] cpu_state = 0, sys_state = 0;
    reg [1:0] response_sync = 0, request_sync = 0, valid_sync = 0 /* synthesis syn_preserve=1 */;
    reg response_seen = 0, request_seen = 0;
    assign cpu_busy = cpu_state != 0;
    assign address = held_address;
    assign map_register = held_map;
    assign write_data = held_data;
    assign lanes = held_lanes;
    assign write = held_write;
    assign request = sys_state == 1 && valid_sync[1];
    always @(posedge cpu_clk or posedge cpu_reset) begin
        if (cpu_reset) begin
            cpu_state <= 0; cpu_ack <= 0; cpu_read <= 0;
            request_toggle <= 0; response_sync <= 0; response_seen <= 0;
            valid <= 0; cancelled <= 0;
            held_address <= 0; held_data <= 0; held_lanes <= 0; held_write <= 0;
            held_map <= 0;
        end else begin
            response_sync <= {response_sync[0], response_toggle};
            case (cpu_state)
                0: if (cpu_cycle && cpu_strobe) begin
                    held_address <= cpu_address; held_data <= cpu_data;
                    held_map <= cpu_map_register;
                    held_lanes <= cpu_lanes; held_write <= cpu_write;
                    valid <= 1; cancelled <= 0;
                    request_toggle <= !request_toggle; cpu_state <= 1;
                end
                1: begin
                    if (!cpu_cycle || !cpu_strobe) begin valid <= 0; cancelled <= 1; end
                    if (response_sync[1] != response_seen) begin
                        response_seen <= response_sync[1];
                        if (cancelled || !cpu_cycle || !cpu_strobe) cpu_state <= 0;
                        else begin cpu_read <= held_read; cpu_ack <= 1; cpu_state <= 2; end
                    end
                end
                2: if (!cpu_cycle || !cpu_strobe) begin
                    cpu_ack <= 0; valid <= 0; cpu_state <= 3;
                end
                3: if (response_sync[1] == request_toggle) cpu_state <= 0;
            endcase
        end
    end
    always @(posedge sys_clk or posedge sys_reset) begin
        if (sys_reset) begin
            sys_state <= 0; request_sync <= 0; valid_sync <= 0;
            response_toggle <= 0; request_seen <= 0; held_read <= 0;
        end else begin
            request_sync <= {request_sync[0], request_toggle};
            valid_sync <= {valid_sync[0], valid};
            case (sys_state)
                0: if (request_sync[1] != request_seen) begin
                    request_seen <= request_sync[1]; sys_state <= 1;
                end
                1: if (ready || !valid_sync[1]) begin
                    held_read <= ready ? read_data : 16'b0;
                    response_toggle <= request_seen; sys_state <= 2;
                end
                2: if (!valid_sync[1]) sys_state <= 0;
            endcase
        end
    end
endmodule
