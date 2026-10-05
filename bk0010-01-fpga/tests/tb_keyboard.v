`timescale 1ns/1ps
module tb_keyboard;
    reg clk = 0, reset = 1, clock_low = 0, serial_data = 1, ready = 1;
    always #20.833333 clk = !clk;
    tri1 ps2_clock;
    assign ps2_clock = clock_low ? 1'b0 : 1'bz;
    wire [8:0] scan;
    wire pop, valid, held, stop;
    wire [7:0] code;
    reg [7:0] received[0:63];
    integer count = 0, previous, i;
    uj11_ps2_keyboard #(.CLOCK_HZ(24000000)) transport(clk,reset,ps2_clock,serial_data,pop,scan);
    bk_keymap map(clk,reset,scan[8],scan[7:0],pop,valid,code,ready,held,stop);
    always @(posedge clk) if(!reset && valid && ready) begin received[count] = code; count = count + 1; end
    task bit_send(input value);
        begin
            serial_data = value; #15000; clock_low = 1; #30000; clock_low = 0; #15000;
        end
    endtask
    task send(input [7:0] value, input bad_parity);
        integer n;
        begin
            wait(ps2_clock); #10000;
            bit_send(0);
            for(n=0;n<8;n=n+1) bit_send(value[n]);
            bit_send((~^value) ^ bad_parity); bit_send(1);
            serial_data = 1; #50000;
        end
    endtask
    task release_key(input [7:0] value);
        begin send(8'hf0,0); send(value,0); end
    endtask
    task expect_key(input [7:0] value);
        begin
            if(count != previous+1 || received[previous] !== value)
                $fatal(1, "Key expected %h index=%0d count=%0d got=%h",value,previous,count,received[previous]);
            previous = count;
        end
    endtask
    initial begin
        #2000; reset = 0; previous = 0;
        send(8'h1c,0); expect_key("a"); if(!held) $fatal(1,"Make held");
        release_key(8'h1c); if(held) $fatal(1,"Break held");
        send(8'h12,0); if(held) $fatal(1,"Shift repeats previous character");
        send(8'h1c,0); expect_key("A"); release_key(8'h1c);
        send(8'h55,0); expect_key("+"); release_key(8'h55); release_key(8'h12);
        send(8'h58,0); send(8'h58,0); release_key(8'h58);
        send(8'h1c,0); expect_key("A"); release_key(8'h1c);
        send(8'h12,0); send(8'h1c,0); expect_key("a"); release_key(8'h1c); release_key(8'h12);
        send(8'h14,0); send(8'h34,0); expect_key(8'o7); release_key(8'h34); release_key(8'h14);
        send(8'he0,0); send(8'h11,0); send(8'h1c,0); expect_key(8'hc1);
        release_key(8'h1c); send(8'he0,0); release_key(8'h11);
        send(8'he0,0); send(8'h75,0); expect_key(8'o32); send(8'he0,0); release_key(8'h75);
        send(8'h5a,0); expect_key(8'o12); release_key(8'h5a);
        send(8'h66,0); expect_key(8'o30); release_key(8'h66);
        send(8'h76,0); if(!stop) $fatal(1,"STOP make"); release_key(8'h76); if(stop) $fatal(1,"STOP break");
        // Pause's embedded Ctrl scan must not leave Ctrl held.
        send(8'he1,0); send(8'h14,0); send(8'h77,0); send(8'he1,0);
        send(8'hf0,0); send(8'h14,0); send(8'hf0,0); send(8'h77,0);
        send(8'h34,0); expect_key("G"); release_key(8'h34);
        send(8'h12,0); send(8'h1c,1); // Fault marker clears modifiers and held state.
        if(held) $fatal(1,"Parity fault retained held state");
        send(8'h1c,0); expect_key("A"); release_key(8'h1c);
        ready = 0; send(8'h21,0); send(8'h23,0);
        if(!valid || code != "C" || count != previous) $fatal(1,"Backpressure changed pending key");
        #100000; ready = 1; #100000;
        if(count != previous+2 || received[previous] != "C" || received[previous+1] != "D")
            $fatal(1,"Buffered scan order");
        $display("PASS physical PS/2: make/break, Shift/Caps/Ctrl/Alt, arrows, STOP, Pause, bad parity, backpressure");
        $finish;
    end
    initial begin #100000000; $fatal(1,"Keyboard timeout"); end
endmodule
