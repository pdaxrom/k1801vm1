`timescale 1ns/1ps
// The output latch interrupts the guest's VT handler at 164 even with IE clear.
// Reading EDB acknowledges that latch; EDBA intentionally has no side effects.
module uj11_terak_console #(parameter integer CLOCK_HZ=50000000)(
    input wire clk,reset,bus_reset,request,writing,
    input wire [12:0] address,
    input wire [1:0] lanes,
    input wire [15:0] write_data,
    output reg [15:0] read_data,
    output reg [15:0] gar,vir,vcr,
    input wire key_valid,
    input wire [7:0] key_data,
    output wire key_ready,
    output wire irq,
    output wire [15:0] vector,
    input wire irq_ack,
    output wire [1:0] audio
);
    reg seen,key_done,key_ie,output_done,output_ie;
    reg key_irq,printer_irq,emulator_irq;
    reg [7:0] key_latch;
    reg [15:0] output_latch;
    reg [31:0] printer_delay,tone_divider;
    reg tone;
    wire access=request && !seen;
    assign key_ready=!key_done;
    assign irq=emulator_irq || key_irq || printer_irq;
    assign vector=emulator_irq ? 16'o164 : key_irq ? 16'o60 : 16'o64;
    assign audio={2{vcr[8] || (vcr[11] && tone)}};
    always @*begin
        case(address)
            13'o17560:read_data={8'b0,key_done,key_ie,6'b0};
            13'o17562:read_data={8'b0,key_latch};
            // Bit 11 advertises a line-clock interrupt latency below 300 us.
            13'o17564:read_data=16'o4000 | {8'b0,output_done,output_ie,6'b0};
            13'o17566,13'o17746:read_data=output_latch;
            13'o17740:read_data=gar;
            13'o17742:read_data=vir;
            13'o17744:read_data=vcr;
            default:read_data=0;
        endcase
    end
    always @(posedge clk)begin
        if(reset || bus_reset)begin
            seen<=0;key_done<=0;key_ie<=0;output_done<=1;output_ie<=0;
            key_irq<=0;printer_irq<=0;emulator_irq<=0;key_latch<=0;output_latch<=0;
            printer_delay<=0;gar<=0;vir<=0;vcr<=0;tone_divider<=0;tone<=0;
        end else begin
            if(!request)seen<=0;
            if(request)seen<=1;
            if(tone_divider==CLOCK_HZ/1560-1)begin tone_divider<=0;tone<=!tone;end
            else tone_divider<=tone_divider+1'b1;
            if(irq_ack)begin
                if(emulator_irq)emulator_irq<=0;
                else if(key_irq)key_irq<=0;
                else printer_irq<=0;
            end
            if(printer_delay!=0)begin
                printer_delay<=printer_delay-1'b1;
                if(printer_delay==1 && output_ie && output_done)printer_irq<=1;
            end
            if(key_valid && key_ready)begin
                key_latch<=key_data;key_done<=1;
                if(key_ie)key_irq<=1;
            end
            if(access)case(address)
                13'o17560:if(writing && lanes[0])begin
                    key_ie<=write_data[6];key_irq<=write_data[6] && key_done;
                end
                13'o17562:if(!writing)begin key_done<=0;key_irq<=0;end
                13'o17564:if(writing && lanes[0])begin
                    output_ie<=write_data[6];
                    if(!write_data[6])printer_irq<=0;
                    else if(!output_ie && output_done)printer_delay<=CLOCK_HZ/1000000*480;
                end
                13'o17566:begin
                    if(writing)begin
                        if(lanes[0])output_latch[7:0]<=write_data[7:0];
                        if(lanes[1])output_latch[15:8]<=write_data[15:8];
                        output_done<=0;emulator_irq<=1;
                        printer_irq<=0;printer_delay<=0;
                    end else if(!writing)begin
                        output_done<=1;emulator_irq<=0;printer_delay<=CLOCK_HZ/1000000*480;
                    end
                end
                13'o17746:if(writing)begin
                    if(lanes[0])output_latch[7:0]<=write_data[7:0];
                    if(lanes[1])output_latch[15:8]<=write_data[15:8];
                end
                13'o17740:if(writing && lanes==3)gar<={write_data[15:1],1'b0};
                13'o17742:if(writing && lanes==3)vir<=write_data;
                13'o17744:if(writing)begin
                    if(lanes[0])vcr[7:0]<=write_data[7:0];
                    if(lanes[1])vcr[15:8]<=write_data[15:8];
                end
                default:begin end
            endcase
        end
    end
endmodule
