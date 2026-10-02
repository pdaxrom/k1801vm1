`timescale 1ns/1ps
// Independent RESET-button controller. Never reset this block with its own
// hard_reset output or with the guest's PDP-11 RESET instruction.
module uj11_button #(
    parameter integer SAMPLE_DIVISOR=591200, HOLD_SAMPLES=100
) (
    input wire clk, power_on, button_n,
    output reg hard_reset,
    output reg halt_pulse
);
    reg [1:0] sync;
    reg previous, pressed;
    reg [6:0] duration;
    localparam integer LAST_HOLD=HOLD_SAMPLES-1;
    wire sample;
    uj11_tick #(.DIVISOR(SAMPLE_DIVISOR)) timebase(clk,power_on,sample);
    always @(posedge clk) begin
        if(power_on) begin
            sync<=0;
            previous<=0;
            pressed<=1;
            duration<=0;
            hard_reset<=1;
            halt_pulse<=0;
        end else begin
            sync<={sync[0],button_n};
            halt_pulse<=0;
            if(sample) begin
                previous<=sync[1];
                // Two equal samples confirm either edge. Boot starts in
                // reset and requires a confirmed release, also for held power-on.
                if(sync[1]==previous) begin
                    if(sync[1]) begin
                        if(pressed && !hard_reset) halt_pulse<=1;
                        pressed<=0;
                        hard_reset<=0;
                        duration<=0;
                    end else begin
                        pressed<=1;
                        if(!hard_reset) begin
                            if(duration==LAST_HOLD[6:0]) hard_reset<=1;
                            else duration<=duration+1'b1;
                        end
                    end
                end
            end
        end
    end
`ifndef SYNTHESIS
    initial if(HOLD_SAMPLES<2 || HOLD_SAMPLES>128)
        $fatal(1,"button HOLD_SAMPLES outside 2..128");
`endif
endmodule
