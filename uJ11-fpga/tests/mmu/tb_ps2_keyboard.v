`timescale 1ns/1ps
module tb_ps2_keyboard;
    reg clk=0,reset=1,pop=0,device_clock=0,data=1;
    always #10 clk=~clk;
    tri1 ps2_clock;
    assign ps2_clock=device_clock ? 1'b0 : 1'bz;
    wire [8:0] value;
    uj11_ps2_keyboard dut(.clk(clk),.reset(reset),.ps2_clock(ps2_clock),.ps2_data(data),.pop(pop),.value(value));
    integer checks=0;
    task bit_send(input reg b);
        begin
            data=b;#7000;device_clock=1;#35000;device_clock=0;#35000;
        end
    endtask
    task frame(input reg [7:0] b,input reg wrong_parity,input reg wrong_stop);
        begin
            if(!ps2_clock)$fatal(1,"device sent while host inhibits");
            bit_send(0);
            for(integer i=0;i<8;i++)bit_send(b[i]);
            bit_send((~^b)^wrong_parity);bit_send(!wrong_stop);data=1;#10000;
        end
    endtask
    task take(input reg [7:0] wanted);
        begin
            if(value!={1'b1,wanted})$fatal(1,"scan got %x wanted %x",value,wanted);
            @(negedge clk);pop=1;@(negedge clk);pop=0;checks++;
        end
    endtask
    initial begin
        #100;reset=0;#10000;
        device_clock=1;#100;device_clock=0;#10000;
        if(value)$fatal(1,"glitch produced scan");
        frame(8'h1c,0,0);take(8'h1c);
        frame(8'he0,0,0);frame(8'hf0,0,0);frame(8'h75,0,0);
        take(8'he0);take(8'hf0);take(8'h75);
        frame(8'h1c,1,0);take(0);frame(8'h1c,0,1);take(0);
        bit_send(0);bit_send(1);data=1;#1100000;take(0);
        frame(8'h32,0,0);take(8'h32);
        for(integer i=0;i<14;i++)frame(i+1,0,0);
        if(ps2_clock)$fatal(1,"FIFO did not inhibit");
        take(1);take(2);
        #30000;if(ps2_clock)$fatal(1,"host inhibition shorter than 100 us");
        #120000;if(!ps2_clock)$fatal(1,"FIFO failed to release clock");
        frame(8'h55,0,0);
        for(integer i=3;i<=14;i++)take(i);
        take(8'h55);
        if(value)$fatal(1,"FIFO empty flag");
        frame(8'h12,0,0);reset=1;#100;reset=0;#10000;
        if(value || !ps2_clock)$fatal(1,"reset did not empty/release");
        frame(8'h1c,0,0);take(8'h1c);
        $display("PASS PS2 framing/filter/parity/timeout/FIFO/inhibit/reset checks=%0d",checks);
        $finish;
    end
    initial begin #40000000;$fatal(1,"PS2 test timeout");end
endmodule
