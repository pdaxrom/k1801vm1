`timescale 1ns/1ps
module tb_pal_encoder;
    reg clk=0,reset=1,sync=0,burst=0,active=0,alt=0;
    reg [7:0] rgb=0;
    wire [5:0] dac;
    always #7.8125 clk=~clk;
    uj11_pal_encoder dut(.clk(clk),.reset(reset),.sync(sync),.burst(burst),.active(active),.v_alternate(alt),.rgb(rgb),.dac(dac));
    reg [31:0] phase=0;
    real expected[0:4];
    real r,g,b,y,u,v,want,error,max_error=0;
    integer valid=0,checks=0;
    always @(posedge clk)begin
        if(reset)begin phase=0;valid=0;for(integer i=0;i<5;i++)expected[i]=0;end
        else begin
            r=real'(rgb[7:5])/7.0;g=real'(rgb[4:2])/7.0;b=real'(rgb[1:0])/3.0;
            y=0.299*r+0.587*g+0.114*b;u=0.493*(b-y);v=0.877*(r-y);
            if(burst)begin y=0;u=-7.5/$sqrt(2.0)/35.0;v=-u;end
            else if(!active)begin y=0;u=0;v=0;end
            if(alt)v=-v;
            want=sync ? 0.0 : 15.0+35.0*(y+u*$sin(real'(phase[31:29])*3.141592653589793/4.0)+
                v*$cos(real'(phase[31:29])*3.141592653589793/4.0));
            for(integer i=4;i>0;i--)expected[i]=expected[i-1];
            expected[0]=want;phase=phase+32'd297535118;valid++;
            #1;
            if(valid>=5)begin
                error=real'(dac)-expected[4];if(error<0)error=-error;
                if(error>max_error)max_error=error;
                if(error>2.1)$fatal(1,"PAL encoder/reference error %.3f DAC=%d expected=%.3f",error,dac,expected[4]);
                checks++;
            end
        end
    end
    initial begin
        repeat(5)@(negedge clk);reset=0;active=1;
        for(integer a=0;a<2;a++)for(integer c=0;c<256;c++)begin
            @(negedge clk);rgb=c;alt=a;
            repeat(64)@(negedge clk);
        end
        // Abrupt white, sync, blank and burst transitions also check pipeline alignment.
        rgb=255;repeat(32)@(negedge clk);active=0;sync=1;
        repeat(301)@(negedge clk);sync=0;
        repeat(57)@(negedge clk);burst=1;
        repeat(144)@(negedge clk);burst=0;
        repeat(24)@(negedge clk);
        $display("PASS PAL encoder: %0d samples, all RGB332 codes/both V phases, max error %.3f DAC codes",checks,max_error);
        $finish;
    end
endmodule
