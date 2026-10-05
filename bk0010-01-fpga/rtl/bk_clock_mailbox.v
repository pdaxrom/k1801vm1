`timescale 1ns/1ps
// Stable three-bit payload with request/acknowledge toggles across clocks.
module bk_clock_mailbox (
    input wire source_clk, target_clk, reset,
    input wire [2:0] requested,
    output reg [2:0] target = 0,
    input wire [2:0] applied,
    output reg [2:0] confirmed = 0
);
    reg [2:0] held = 0;
    reg toggle = 0, ack = 0;
    reg [1:0] ack_sync = 0, request_sync = 0;
    always @(posedge source_clk) begin
        if(reset) begin held <= 0; toggle <= 0; ack_sync <= 0; confirmed <= 0; end
        else begin
            ack_sync <= {ack_sync[0],ack};
            if(ack_sync[1] == toggle) begin
                confirmed <= held;
                if(requested != held) begin held <= requested; toggle <= !toggle; end
            end
        end
    end
    always @(posedge target_clk) begin
        if(reset) begin request_sync <= 0; ack <= 0; target <= 0; end
        else begin
            request_sync <= {request_sync[0],toggle};
            if(request_sync[1] != ack) begin
                target <= held;
                if(applied == held) ack <= request_sync[1];
            end
        end
    end
endmodule
