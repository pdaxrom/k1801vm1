`timescale 1ns/1ps
module tb_panel;
    reg clk = 0, power_on = 1, button_n = 1;
    always #10 clk = !clk;
    wire cpu_p, cpu_n, hard_reset;
    wire [2:0] requested, active;
    reg [2:0] boot_mode = 0;
    reg restore_mode = 0;
    wire full_reset;
    wire [8:0] h, l;
    reg idle = 1;
    bk_panel #(.CLOCK_HZ(32000)) panel (
        .clk(clk), .power_on(power_on), .button_n(button_n), .active_mode(active),
        .requested_mode(requested), .hard_reset(hard_reset), .seg_led_h(h), .seg_led_l(l),
        .boot_mode(boot_mode), .restore_mode(restore_mode), .full_reset(full_reset)
    );
    bk_cpu_clock clocks(clk, power_on, requested, idle, cpu_p, cpu_n, active, 1'b0);
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
    integer mode, ticks = 0, last_rising = 0, last_falling = 0, full_resets = 0;
    integer periods[0:6];
    integer highs[0:6];
    reg [6:0] left_digits[0:6], right_digits[0:6];
    always @(posedge clk) if(full_reset) full_resets <= full_resets + 1;
    always @(posedge clk) ticks <= ticks + 1;
    initial begin
        delay(10); power_on = 0; delay(4000);
        if(hard_reset || active != 0) $fatal(1, "cold boot");
        frame(7'h40, 7'h30);
        periods[0]=16; periods[1]=12; periods[2]=8; periods[3]=6;
        periods[4]=4; periods[5]=3; periods[6]=2;
        highs[0]=8; highs[1]=6; highs[2]=4; highs[3]=3;
        highs[4]=2; highs[5]=1; highs[6]=1;
        left_digits[0]=7'h40; right_digits[0]=7'h30;
        left_digits[1]=7'h40; right_digits[1]=7'h19;
        left_digits[2]=7'h40; right_digits[2]=7'h02;
        left_digits[3]=7'h40; right_digits[3]=7'h00;
        left_digits[4]=7'h79; right_digits[4]=7'h24;
        left_digits[5]=7'h79; right_digits[5]=7'h02;
        left_digits[6]=7'h24; right_digits[6]=7'h19;
        // A pending mode is displayed only after the bus becomes idle.
        idle = 0; press();
        if(requested != 1 || active != 0) $fatal(1, "busy switch");
        frame(7'h40, 7'h30); idle = 1; delay(32);
        for(mode = 1; mode <= 7; mode = mode + 1) begin
            wait(active == mode % 7);
            frame(left_digits[mode % 7], right_digits[mode % 7]);
            @(posedge cpu_p); last_rising = ticks;
            repeat(8) begin
                @(negedge cpu_p); last_falling = ticks;
                if(ticks-last_rising != highs[mode % 7]) $fatal(1, "CPU high phase mode %0d", mode % 7);
                @(posedge cpu_p);
                if(ticks-last_rising != periods[mode % 7]) $fatal(1, "CPU period mode %0d", mode % 7);
                if(ticks-last_falling != periods[mode % 7]-highs[mode % 7])
                    $fatal(1, "CPU low phase mode %0d", mode % 7);
                last_rising = ticks;
            end
            if(mode != 7) press();
        end
        // BIOS can request the stock 4 MHz of BK-0011M.
        boot_mode = 1; restore_mode = 1; delay(3); restore_mode = 0; delay(32);
        if(active != 1) $fatal(1, "BIOS stock frequency");
        press(); if(active != 2) $fatal(1, "4 MHz short press must select 6 MHz");
        // A two-second reset restores the configured boot frequency.
        button_n = 0; delay(70000);
        if(!hard_reset || active != 1 || full_resets) $fatal(1, "warm reset");
        frame(7'h40, 7'h19); button_n = 1; delay(4000);
        if(hard_reset || requested != 1) $fatal(1, "warm release changed mode");
        press(); if(active != 2) $fatal(1, "post-reset short press");
        // Ten seconds generates exactly one full BIOS reset until release.
        button_n = 0; delay(330000);
        if(!hard_reset || full_resets != 1) $fatal(1, "full reset");
        delay(10000); if(full_resets != 1) $fatal(1, "repeated full reset");
        button_n = 1; delay(4000);
        if(hard_reset || requested != 1) $fatal(1, "full release changed frequency");
        $display("PASS panel: 3/4/6/8/12/16/24 MHz periods/phases, applied digits, wrap, warm/full reset");
        $finish;
    end
    initial begin #100000000; $fatal(1, "panel timeout"); end
endmodule
