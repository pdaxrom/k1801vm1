`timescale 1ns/1ps
// AY-3-8910 digital generators. tick is the master clock divided by eight.
// Envelope has 16 levels (AY, not the 32-level YM2149 variant).
module bk_ay (
    input wire clk, reset, tick, write,
    input wire [3:0] address,
    input wire [7:0] data,
    output wire [8:0] pcm
);
    reg [7:0] registers[0:13];
    reg [11:0] tone_count[0:2];
    reg [2:0] tone = 0;
    reg [4:0] noise_count = 0;
    reg noise_divide = 0;
    reg [16:0] noise = 17'b1;
    reg [16:0] envelope_count = 0;
    reg [3:0] envelope_step = 0;
    reg envelope_attack = 0, envelope_hold = 1;
    wire [3:0] envelope = envelope_step ^ {4{envelope_attack}};
    wire [16:0] envelope_period = {registers[12],registers[11],1'b0};
    wire [16:0] envelope_next = envelope_count + 1'b1;
    wire [4:0] noise_next = noise_count + 1'b1;
    integer i;

    function [6:0] amplitude(input [3:0] level);
        begin
            // Logarithmic approximation; analogue load/offset are not modelled.
            case(level)
                0: amplitude=0;    1: amplitude=1;    2: amplitude=2;    3: amplitude=3;
                4: amplitude=4;    5: amplitude=6;    6: amplitude=8;    7: amplitude=12;
                8: amplitude=16;   9: amplitude=23;  10: amplitude=32;  11: amplitude=45;
               12: amplitude=64;  13: amplitude=80;  14: amplitude=101; 15: amplitude=127;
            endcase
        end
    endfunction

    wire [6:0] channel[0:2];
    genvar c;
    generate for(c=0;c<3;c=c+1) begin: voice
        wire [11:0] period = {registers[c*2+1][3:0],registers[c*2]};
        wire [11:0] next_count = tone_count[c] + 1'b1;
        wire gate = (tone[c] || registers[7][c]) && (noise[0] || registers[7][c+3]);
        wire [3:0] level = registers[c+8][4] ? envelope : registers[c+8][3:0];
        assign channel[c] = amplitude(gate ? level : 4'b0);
        always @(posedge clk) begin
            if(reset) tone_count[c] <= 0;
            else if(tick) begin
                if(next_count >= period || period == 0) tone_count[c] <= 0;
                else tone_count[c] <= next_count;
            end
        end
        always @(posedge clk) begin
            if(reset) tone[c] <= 0;
            else if(tick && (next_count >= period || period == 0)) tone[c] <= !tone[c];
        end
    end endgenerate
    assign pcm = {2'b0,channel[0]} + {2'b0,channel[1]} + {2'b0,channel[2]};

    always @(posedge clk) begin
        if(reset) begin
            for(i=0;i<14;i=i+1) registers[i] <= 0;
        end else if(write && address < 14) begin
            case(address)
                1,3,5,13: registers[address] <= data & 8'h0f;
                6,8,9,10: registers[address] <= data & 8'h1f;
                default: registers[address] <= data;
            endcase
        end
        if(reset) begin
            noise_count <= 0; noise_divide <= 0; noise <= 17'b1;
        end else if(tick) begin
            if(noise_next >= registers[6][4:0] || registers[6][4:0] == 0) begin
                noise_count <= 0; noise_divide <= !noise_divide;
                if(noise_divide) noise <= {noise[0]^noise[3],noise[16:1]};
            end else noise_count <= noise_next;
        end
        if(reset) begin
            envelope_count <= 0; envelope_step <= 0; envelope_attack <= 0; envelope_hold <= 1;
        end else if(write && address == 13) begin
            // Every R13 write restarts the envelope, including the same shape.
            envelope_count <= 0; envelope_step <= 15; envelope_attack <= data[2]; envelope_hold <= 0;
        end else if(tick && !envelope_hold) begin
            // A zero envelope period is half a period of one, unlike tone/noise.
            if(envelope_next >= envelope_period || envelope_period == 0) begin
                envelope_count <= 0;
                if(envelope_step != 0) envelope_step <= envelope_step - 1'b1;
                else if(!registers[13][3]) begin
                    envelope_step <= 0; envelope_attack <= 0; envelope_hold <= 1;
                end else if(registers[13][0]) begin
                    envelope_step <= 0; envelope_hold <= 1;
                    if(registers[13][1]) envelope_attack <= !envelope_attack;
                end else begin
                    envelope_step <= 15;
                    if(registers[13][1]) envelope_attack <= !envelope_attack;
                end
            end else envelope_count <= envelope_next;
        end
    end
endmodule
