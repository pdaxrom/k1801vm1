`timescale 1ns/1ps
// One fixed clock: short presses are ignored, 2 s resets the guest, 10 s
// restarts SERV into the BIOS after its current SD operation has finished.
module uj11_terak_button #(
    parameter integer SAMPLE_DIVISOR=1000000,MEDIUM_SAMPLES=100,LONG_SAMPLES=500
)(
    input wire clk,power_on,button_n,storage_idle,
    output wire guest_reset,cold_reset
);
    reg [1:0] sync=3;
    reg [$clog2(SAMPLE_DIVISOR+1)-1:0] divider=0;
    reg [8:0] duration=0;
    reg previous=1,released=1,pending=0;
    reg [2:0] cold=0;
    assign guest_reset=duration>=MEDIUM_SAMPLES || pending || |cold;
    assign cold_reset=|cold;
    always @(posedge clk)begin
        sync<={sync[0],button_n};
        if(power_on)begin
            divider<=0;duration<=0;previous<=1;released<=1;pending<=0;cold<=0;
        end else begin
            cold<={cold[1:0],1'b0};
            if(pending && storage_idle)begin pending<=0;cold<=7;end
            if(divider==SAMPLE_DIVISOR-1)begin
                divider<=0;previous<=sync[1];
                if(previous==sync[1])released<=sync[1];
                if(released)duration<=0;
                else if(duration<LONG_SAMPLES)begin
                    duration<=duration+1'b1;
                    if(duration==LONG_SAMPLES-1)pending<=1;
                end
            end else divider<=divider+1'b1;
        end
    end
endmodule
