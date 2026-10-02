`timescale 1ns/1ps
// Console copy queue. No serial decoder, CPU interrupt or EBR is required.
// Backpressure only applies to TBUF writes after firmware enables the mirror.
module uj11_console_fifo(
    input wire clk,reset,push,pop,configure,enable,
    input wire [7:0] data,
    output wire ready,
    output wire [31:0] value,
    output wire [4:0] level,
    output reg enabled
);
    reg [7:0] bytes[0:15] /* synthesis syn_ramstyle="distributed_ram" */;
    reg [3:0] head,tail;
    reg [4:0] count;
    wire taking=pop && count!=0;
    wire adding=push && enabled && ready;
    assign ready=!enabled || count<16;
    assign level=count;
    assign value={23'b0,count!=0, count!=0 ? bytes[tail] : 8'b0};
    always @(posedge clk)begin
        if(reset)begin head<=0;tail<=0;count<=0;enabled<=0;end
        else begin
            if(configure)enabled<=enable;
            if(adding)begin bytes[head]<=data;head<=head+1'b1;end
            if(taking)tail<=tail+1'b1;
            case({adding,taking})
                2'b10:count<=count+1'b1;
                2'b01:count<=count-1'b1;
                default:begin end
            endcase
        end
    end
endmodule
