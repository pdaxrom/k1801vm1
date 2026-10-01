`timescale 1ns/1ps
// Asynchronous SRAM, registered address/data/control phases. 24 MHz system.
// One setup cycle, two access cycles, one hold/release cycle. A complete idle
// cycle separates DQ owners. Only configuration/PLL loss clears memory;
// CPU/peripheral reset cancels the response but finishes an active SRAM cycle
// with its normal hold time. Thus reset cannot truncate WE or release DQ early.
module uj11_sram #(
    parameter integer CLEAR_WORDS=65536, FAST_RESPONSE=0
) (
    input wire clk, power_on, reset, request, write,
    input wire [19:0] address,
    input wire [1:0] byte_enable,
    input wire [15:0] write_data,
    output reg [15:0] read_data,
    output wire ready, output reg initialized,
    output reg [19:0] sram_address,
    inout wire [15:0] sram_data,
    output reg sram_ce_n=1, sram_oe_n=1, sram_we_n=1, sram_lb_n=1, sram_ub_n=1
);
    localparam IDLE=0, SETUP=1, ACCESS1=2, ACCESS2=3, HOLD=4, RELEASE=5, DONE=6;
    reg [2:0] state;
    reg writing;
    reg cancelled;
    reg response_valid;
    // Early response overlaps the unchanged pin hold/release phases. The
    // request qualifier prevents an old completion reaching the next owner.
    assign ready=response_valid && (!FAST_RESPONSE || request);
    reg [15:0] data_out;
    reg [19:0] clear_address;
    reg drive_data=0;
    assign sram_data=drive_data ? data_out : 16'bz;

    always @(posedge clk) begin
        if(power_on) begin
            state<=IDLE; initialized<=0; clear_address<=0;
            sram_ce_n<=1; sram_oe_n<=1; sram_we_n<=1;
            sram_lb_n<=1; sram_ub_n<=1; drive_data<=0;
            response_valid<=0; read_data<=0; writing<=0; cancelled<=0;
            sram_address<=0; data_out<=0;
        end else if(initialized && reset && (state==IDLE || state==DONE)) begin
            state<=IDLE; response_valid<=0;
            sram_ce_n<=1; sram_oe_n<=1; sram_we_n<=1;
            sram_lb_n<=1; sram_ub_n<=1; drive_data<=0;
        end else begin
        if(initialized && reset) begin cancelled<=1;response_valid<=0;end
        case(state)
            IDLE: begin
                response_valid<=0;
                cancelled<=0;
                if(!initialized || request) begin
                    sram_address<=initialized ? address : clear_address;
                    writing<=!initialized || write;
                    data_out<=initialized ? write_data : 16'b0;
                    sram_ce_n<=0;
                    sram_lb_n<=initialized ? !byte_enable[0] : 1'b0;
                    sram_ub_n<=initialized ? !byte_enable[1] : 1'b0;
                    drive_data<=!initialized || write;
                    state<=SETUP;
                end
            end
            SETUP: begin
                sram_oe_n<=writing; sram_we_n<=!writing; state<=ACCESS1;
            end
            ACCESS1: state<=ACCESS2;
            ACCESS2: begin
                if(!writing) read_data<=sram_data;
                if(FAST_RESPONSE && initialized && !cancelled && !reset)response_valid<=1;
                sram_oe_n<=1; sram_we_n<=1; state<=HOLD;
            end
            HOLD: begin
                sram_ce_n<=1; sram_lb_n<=1; sram_ub_n<=1; drive_data<=0;
                state<=RELEASE;
            end
            RELEASE: begin
                if(!initialized) begin
                    if(clear_address==CLEAR_WORDS-1) initialized<=1;
                    else clear_address<=clear_address+1'b1;
                    state<=IDLE;
                end else if(cancelled || reset) begin response_valid<=0; state<=IDLE; end
                else if(FAST_RESPONSE && !request)begin response_valid<=0;state<=IDLE;end
                else begin response_valid<=1; state<=DONE; end
            end
            DONE: if(!request) begin response_valid<=0; state<=IDLE; end
            default: begin state<=IDLE; response_valid<=0; end
        endcase
        end
    end
`ifndef SYNTHESIS
    initial if(CLEAR_WORDS<1 || CLEAR_WORDS>1048576) $fatal(1,"Invalid SRAM clear size");
`endif
endmodule
