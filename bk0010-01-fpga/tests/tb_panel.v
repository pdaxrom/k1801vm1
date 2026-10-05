`timescale 1ns/1ps
module tb_panel;
    reg clk = 0, power_on = 1, button_n = 1;
    always #10 clk = !clk;
    wire cpu_p, cpu_n, hard_reset;
    wire [1:0] requested, active;
    wire [8:0] h, l;
    reg idle = 1;
    bk_panel #(.CLOCK_HZ(32000)) panel (
        .clk(clk), .power_on(power_on), .button_n(button_n), .active_mode(active),
        .requested_mode(requested), .hard_reset(hard_reset), .seg_led_h(h), .seg_led_l(l)
    );
    bk_cpu_clock clocks(clk, power_on, requested, idle, cpu_p, cpu_n, active);
    task delay(input integer cycles);
        repeat(cycles) @(negedge clk);
    endtask
    task frame(input [6:0] left, right);
        reg [15:0] seen, lit, expected;
        integer n, counts[0:15];
        begin
            delay(128);
            seen = 0; expected = {1'b0, ~right, 1'b0, ~left};
            for(n = 0; n < 16; n = n + 1) counts[n] = 0;
            repeat(64) begin
                @(negedge clk);
                lit = {l[8] ? ~l[7:0] : 8'b0, h[8] ? ~h[7:0] : 8'b0};
                seen = seen | lit;
                for(n = 0; n < 16; n = n + 1) if(lit[n]) counts[n] = counts[n] + 1;
            end
            if(seen !== expected) $fatal(1, "frequency glyph %h expected %h", seen, expected);
            for(n = 0; n < 16; n = n + 1)
                if(counts[n] != (expected[n] ? 1 : 0)) $fatal(1, "segment duty");
        end
    endtask
    task press;
        begin button_n = 0; delay(4000); button_n = 1; delay(4000); end
    endtask
    reg [8:0] prev_h = 9'h0ff, prev_l = 9'h0ff;
    reg [15:0] cathodes;
    always @(negedge clk) begin
        if(cpu_n !== !cpu_p) $fatal(1, "CPU phases");
        cathodes = {~l[7:0], ~h[7:0]};
        if((cathodes & (cathodes - 1'b1)) != 0 || (h[8] && l[8])) $fatal(1, "multiple segments");
        if(!power_on && (h[7:0] != prev_h[7:0] || l[7:0] != prev_l[7:0]))
            if(h[8] || l[8] || prev_h[8] || prev_l[8]) $fatal(1, "no blank interval");
        prev_h = h; prev_l = l;
    end
    integer mode, saved_mode, ticks = 0, last_rising = 0;
    always @(posedge clk) ticks <= ticks + 1;
    initial begin
        delay(10); power_on = 0; delay(4000);
        if(hard_reset || active != 0) $fatal(1, "cold boot");
        frame(7'h40, 7'h30);
        // A pending mode must not be displayed saved_mode the bus is idle.
        idle = 0; press();
        if(requested != 1 || active != 0) $fatal(1, "busy switch");
        frame(7'h40, 7'h30); idle = 1; delay(32);
        frame(7'h40, 7'h02);
        press(); frame(7'h79, 7'h24);
        press(); frame(7'h24, 7'h19);
        // Exact rising-edge intervals in every mode: /16,/8,/4,/2.
        for(mode = 3; mode >= 0; mode = mode - 1) begin
            wait(active == mode); @(posedge cpu_p); last_rising = ticks;
            repeat(8) begin
                @(posedge cpu_p);
                if(ticks-last_rising != (16 >> mode)) $fatal(1, "CPU frequency interval");
                last_rising = ticks;
            end
            if(mode != 0) begin
                // Cycle until the next mode under test, via wraparound.
                repeat(3) press();
            end
        end
        frame(7'h40, 7'h30);
        // Long press resets, release must not count as a speed request.
        saved_mode = requested; button_n = 0; delay(70000);
        if(!hard_reset) $fatal(1, "long reset");
        frame(7'h40, 7'h30); button_n = 1; delay(4000);
        if(hard_reset || requested != saved_mode) $fatal(1, "long release changed mode");
        $display("PASS panel: frequencies, applied display, wrap, long reset, phases and segment duty");
        $finish;
    end
    initial begin #100000000; $fatal(1, "panel timeout"); end
endmodule
