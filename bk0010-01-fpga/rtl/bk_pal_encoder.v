`timescale 1ns/1ps
// The uJ11 encoder's exact samples, tabulated for all BK palette colours.
// DDS frequency, alternating V and the five-clock sync/video latency are retained.
module bk_pal_encoder(input wire clk,reset,sync,burst,active,v_alternate,
                      input wire [3:0] color_index,output reg [5:0] dac=0);
    reg [31:0] phase=0;
    reg [9:0] address=0;
    reg [3:0] sync_delay=0;
    (* syn_ramstyle="block_ram" *) reg [5:0] samples[0:1023];
    reg [5:0] sample1,sample2=15,sample3=15;
    initial $readmemh("build/pal_samples.hex",samples);
    always @(posedge clk)sample1<=samples[address];
    always @(posedge clk)begin
        if(reset)begin
            phase<=0;address<=0;sync_delay<=0;sample2<=15;sample3<=15;dac<=0;
        end else begin
            phase<=phase+32'd297535118;
            address<={active,burst,v_alternate,phase[31:29],color_index};
            sync_delay<={sync_delay[2:0],sync};
            sample2<=sample1;sample3<=sample2;
            dac<=sync_delay[3]?6'b0:sample3;
        end
    end
endmodule
