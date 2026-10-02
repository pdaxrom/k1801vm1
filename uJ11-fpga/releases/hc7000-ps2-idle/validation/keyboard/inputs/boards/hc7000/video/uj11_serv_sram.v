`timescale 1ns/1ps
// RV32 data access to reserved SRAM, split into two ordinary 16-bit cycles.
// A result remains stable until SERV withdraws its Wishbone request.
module uj11_serv_sram(
    input wire clk,reset,cycle,write,
    input wire [20:0] address,
    input wire [31:0] data,
    input wire [3:0] lanes,
    output wire acknowledged,
    output reg [31:0] result,
    output wire request,writing,
    output wire [21:0] physical,
    output wire [15:0] output_data,
    output wire [1:0] output_lanes,
    input wire ready,
    input wire [15:0] input_data
);
    localparam IDLE=0,LOW=1,GAP=2,HIGH=3,DONE=4;
    reg [2:0] state;
    reg [20:2] base;
    reg [31:0] held_data;
    reg [3:0] held_lanes;
    reg held_write;
    assign request=state==LOW || state==HIGH;
    assign writing=held_write;
    assign physical={1'b0,base,state==HIGH,1'b0};
    assign output_data=state==HIGH ? held_data[31:16] : held_data[15:0];
    assign output_lanes=state==HIGH ? held_lanes[3:2] : held_lanes[1:0];
    assign acknowledged=state==DONE;
    always @(posedge clk)begin
        if(reset)begin state<=IDLE;result<=0;base<=0;held_data<=0;held_lanes<=0;held_write<=0;end
        else case(state)
            IDLE:if(cycle)begin
                base<=address[20:2];held_data<=data;held_lanes<=lanes;held_write<=write;
                result<=0;
                // The bridge never exposes guest RAM or the I/O page to SERV.
                state<=address<21'h1e4000 ? DONE : write && lanes[1:0]==0 ? HIGH : LOW;
            end
            LOW:if(ready)begin
                result[15:0]<=input_data;
                state<=held_write && held_lanes[3:2]==0 ? DONE : GAP;
            end
            GAP:if(!ready)state<=HIGH;
            HIGH:if(ready)begin result[31:16]<=input_data;state<=DONE;end
            DONE:if(!cycle)state<=IDLE;
            default:state<=IDLE;
        endcase
    end
endmodule
