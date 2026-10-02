`timescale 1ns/1ps
// RGB332 PAL encoder, no multiplier and no EBR. Channel contributions are
// small distributed tables in quarter-DAC units. Eight subcarrier phases
// approximate sin/cos; diagonal phases use 11/16 (versus sqrt(1/2)).
// Black=15, white=50, sync=0; 300:700 sync/luma ratio. Analogue levels and
// composite bandwidth must also be checked on the loaded board output.
// All paths, including sync/blanking, have the same five-clock latency.
module uj11_pal_encoder(
    input wire clk, reset, sync, burst, active, v_alternate,
    input wire [7:0] rgb,
    output reg [5:0] dac
);
    // round(2^32 * 4433618.75 / 64000000), error below 0.008 Hz.
    reg [31:0] phase;
    // Break the raster/line-valid decode from the RGB matrix. This stage
    // keeps the 64 MHz critical path independent of vertical comparators.
    reg [7:0] rgb0;
    reg sync0,burst0,active0,alternate0;
    reg [2:0] p0;
    function signed [8:0] yr;
        input [2:0] c;
        begin case(c)
            3'd0: yr=9'sd0;
            3'd1: yr=9'sd6;
            3'd2: yr=9'sd12;
            3'd3: yr=9'sd18;
            3'd4: yr=9'sd24;
            3'd5: yr=9'sd30;
            3'd6: yr=9'sd36;
            3'd7: yr=9'sd42;
            default: yr=0;
        endcase end
    endfunction
    function signed [8:0] yg;
        input [2:0] c;
        begin case(c)
            3'd0: yg=9'sd0;
            3'd1: yg=9'sd12;
            3'd2: yg=9'sd23;
            3'd3: yg=9'sd35;
            3'd4: yg=9'sd47;
            3'd5: yg=9'sd59;
            3'd6: yg=9'sd70;
            3'd7: yg=9'sd82;
            default: yg=0;
        endcase end
    endfunction
    function signed [8:0] yb;
        input [1:0] c;
        begin case(c)
            2'd0: yb=9'sd0;
            2'd1: yb=9'sd5;
            2'd2: yb=9'sd11;
            2'd3: yb=9'sd16;
            default: yb=0;
        endcase end
    endfunction
    function signed [8:0] ur;
        input [2:0] c;
        begin case(c)
            3'd0: ur=9'sd0;
            3'd1: ur=-9'sd3;
            3'd2: ur=-9'sd6;
            3'd3: ur=-9'sd9;
            3'd4: ur=-9'sd12;
            3'd5: ur=-9'sd15;
            3'd6: ur=-9'sd18;
            3'd7: ur=-9'sd21;
            default: ur=0;
        endcase end
    endfunction
    function signed [8:0] ug;
        input [2:0] c;
        begin case(c)
            3'd0: ug=9'sd0;
            3'd1: ug=-9'sd6;
            3'd2: ug=-9'sd11;
            3'd3: ug=-9'sd17;
            3'd4: ug=-9'sd23;
            3'd5: ug=-9'sd29;
            3'd6: ug=-9'sd34;
            3'd7: ug=-9'sd40;
            default: ug=0;
        endcase end
    endfunction
    function signed [8:0] ub;
        input [1:0] c;
        begin case(c)
            2'd0: ub=9'sd0;
            2'd1: ub=9'sd20;
            2'd2: ub=9'sd41;
            2'd3: ub=9'sd61;
            default: ub=0;
        endcase end
    endfunction
    function signed [8:0] vr;
        input [2:0] c;
        begin case(c)
            3'd0: vr=9'sd0;
            3'd1: vr=9'sd12;
            3'd2: vr=9'sd25;
            3'd3: vr=9'sd37;
            3'd4: vr=9'sd49;
            3'd5: vr=9'sd61;
            3'd6: vr=9'sd74;
            3'd7: vr=9'sd86;
            default: vr=0;
        endcase end
    endfunction
    function signed [8:0] vg;
        input [2:0] c;
        begin case(c)
            3'd0: vg=9'sd0;
            3'd1: vg=-9'sd10;
            3'd2: vg=-9'sd21;
            3'd3: vg=-9'sd31;
            3'd4: vg=-9'sd41;
            3'd5: vg=-9'sd51;
            3'd6: vg=-9'sd62;
            3'd7: vg=-9'sd72;
            default: vg=0;
        endcase end
    endfunction
    function signed [8:0] vb;
        input [1:0] c;
        begin case(c)
            2'd0: vb=9'sd0;
            2'd1: vb=-9'sd5;
            2'd2: vb=-9'sd9;
            2'd3: vb=-9'sd14;
            default: vb=0;
        endcase end
    endfunction
    reg signed [8:0] y1,u1,v1,y2,u2,v2;
    reg [2:0] p1,p2;
    reg a1;
    reg [2:0] sync_delay;
    reg signed [9:0] chroma3;
    reg signed [8:0] y3;
    reg signed [9:0] diagonal;
    wire signed [10:0] mixed=11'sd60+$signed(y3)+$signed(chroma3);
    always @* begin
        case(p2)
            1: diagonal=$signed(u2)+$signed(v2);
            3: diagonal=$signed(u2)-$signed(v2);
            5: diagonal=-$signed(u2)-$signed(v2);
            default: diagonal=-$signed(u2)+$signed(v2);
        endcase
    end
    always @(posedge clk) begin
        if(reset) begin
            rgb0<=0;sync0<=0;burst0<=0;active0<=0;alternate0<=0;p0<=0;
            phase<=0;dac<=0;sync_delay<=0;y1<=0;u1<=0;v1<=0;
            y2<=0;u2<=0;v2<=0;p1<=0;p2<=0;a1<=0;y3<=0;chroma3<=0;
        end else begin
            phase<=phase+32'd297535118;
            rgb0<=rgb;sync0<=sync;burst0<=burst;active0<=active;alternate0<=v_alternate;
            p0<=phase[31:29];p1<=p0;p2<=p1;a1<=alternate0;
            sync_delay<={sync_delay[1:0],sync0};
            y1<=active0 ? yr(rgb0[7:5])+yg(rgb0[4:2])+yb(rgb0[1:0]) : 9'sd0;
            u1<=burst0 ? -9'sd21 : active0 ? ur(rgb0[7:5])+ug(rgb0[4:2])+ub(rgb0[1:0]) : 9'sd0;
            v1<=burst0 ? 9'sd21 : active0 ? vr(rgb0[7:5])+vg(rgb0[4:2])+vb(rgb0[1:0]) : 9'sd0;
            y2<=y1;u2<=u1;v2<=a1 ? -v1 : v1;
            y3<=y2;
            case(p2)
                0: chroma3<=v2;
                2: chroma3<=u2;
                4: chroma3<=-v2;
                6: chroma3<=-u2;
                default: chroma3<=(diagonal>>>1)+(diagonal>>>3)+(diagonal>>>4);
            endcase
            if(sync_delay[2])dac<=0;
            else if(mixed<0)dac<=0;
            else if(mixed>255)dac<=63;
            else dac<=mixed[7:2];
        end
    end
endmodule
