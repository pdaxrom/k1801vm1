`timescale 1ns/1ps
module tb_hc7000_diagnostics;
    parameter integer CLOCK_HZ=1600000;
    localparam integer MS=CLOCK_HZ/1000;
    localparam integer SCAN_DIV=CLOCK_HZ/32000;
    localparam integer FRAME_CYCLES=64*SCAN_DIV;
    reg clk=0;always #5 clk=~clk;
    reg power_on=1,reset_active=0,console_active=0,console_halt=0,waiting=0;
    reg [1:0] mode=0;
    reg retire=0,disk_active=0;
    wire [8:0] seg_led_h,seg_led_l,off_h,off_l;
    uj11_hc7000_diagnostics #(.ENABLE(1),.CLOCK_HZ(CLOCK_HZ)) dut(.*);
    uj11_hc7000_diagnostics #(.CLOCK_HZ(CLOCK_HZ)) disabled_dut(
        .clk(clk),.power_on(power_on),.reset_active(reset_active),
        .console_active(console_active),.console_halt(console_halt),.waiting(waiting),
        .mode(mode),.retire(retire),.disk_active(disk_active),.seg_led_h(off_h),.seg_led_l(off_l));
    integer checks=0,i;
    reg [15:0] cathodes;
    reg [8:0] previous_h=9'h0ff,previous_l=9'h0ff;
    always @(negedge clk)begin
        if(off_h!==9'h0ff || off_l!==9'h0ff)$fatal(1,"default profile energizes display");
        if(seg_led_h[8] && seg_led_l[8])$fatal(1,"both commons enabled");
        cathodes={~seg_led_l[7:0],~seg_led_h[7:0]};
        if($isunknown({seg_led_h,seg_led_l}))$fatal(1,"unknown display output");
        if((cathodes & (cathodes-16'd1))!=0)$fatal(1,"more than one cathode selected across both digits");
        if(seg_led_h[8] && (seg_led_h[7:0]==8'hff || seg_led_l[7:0]!=8'hff))
            $fatal(1,"left common enabled without exactly one left segment");
        if(seg_led_l[8] && (seg_led_l[7:0]==8'hff || seg_led_h[7:0]!=8'hff))
            $fatal(1,"right common enabled without exactly one right segment");
        if(!power_on && (seg_led_h[7:0]!==previous_h[7:0] || seg_led_l[7:0]!==previous_l[7:0]))
            if(seg_led_h[8] || seg_led_l[8] || previous_h[8] || previous_l[8])
                $fatal(1,"cathodes changed without a blank interval");
        previous_h=seg_led_h;previous_l=seg_led_l;
    end
    task delay_ms(input integer ms);
        repeat(ms*MS)@(negedge clk);
    endtask
    task display(input [6:0] high_glyph,input [6:0] low_glyph,input bit cpu_dot,input bit sd_dot);
        reg [15:0] expected_segments,seen_segments,lit_segments;
        integer counts[0:15];
        integer n;
        begin
            delay_ms(4);seen_segments=0;
            expected_segments={sd_dot,~low_glyph,cpu_dot,~high_glyph};
            for(n=0;n<16;n=n+1)counts[n]=0;
            repeat(FRAME_CYCLES)begin
                @(negedge clk);
                lit_segments={seg_led_l[8] ? ~seg_led_l[7:0] : 8'b0,
                              seg_led_h[8] ? ~seg_led_h[7:0] : 8'b0};
                seen_segments=seen_segments | lit_segments;
                if((lit_segments & ~expected_segments)!=0)$fatal(1,"unexpected lit segment");
                for(n=0;n<16;n=n+1)if(lit_segments[n])counts[n]=counts[n]+1;
            end
            if(seen_segments!==expected_segments)$fatal(1,"incomplete frame: %h expected %h",seen_segments,expected_segments);
            for(n=0;n<16;n=n+1)
                if(counts[n]!=(expected_segments[n] ? SCAN_DIV : 0))$fatal(1,"segment %0d has incorrect duty",n);
            checks=checks+1;
        end
    endtask
    initial begin
        delay_ms(2);
        if(seg_led_h!==9'h0ff || seg_led_l!==9'h0ff)$fatal(1,"power-on must blank outputs");
        power_on=0;
        display(7'h12,7'h11,0,0); // SY
        mode=1;display(7'h12,7'h41,0,0); // SU
        mode=3;display(7'h41,7'h12,0,0); // U5
        mode=2;display(7'h3f,7'h3f,0,0); // reserved mode
        mode=3;waiting=1;display(7'h3f,7'h3f,0,0); // WAIT overrides mode
        console_active=1;display(7'h40,7'h21,0,0); // 0d overrides WAIT
        console_halt=1;display(7'h09,7'h47,0,0); // HL
        reset_active=1;display(7'h2f,7'h12,0,0); // r5 overrides HALT
        delay_ms(120);display(7'h2f,7'h12,0,0); // held reset keeps scanning
        reset_active=0;console_active=0;console_halt=0;waiting=0;mode=0;
        retire=1;disk_active=1;@(negedge clk);retire=0;disk_active=0;
        display(7'h12,7'h11,1,1); // single-cycle events remain visible
        delay_ms(110);display(7'h12,7'h11,0,0);
        retire=1;disk_active=1;delay_ms(150);display(7'h12,7'h11,1,1);
        reset_active=1;display(7'h2f,7'h12,0,0); // reset clears dots despite active inputs
        retire=0;disk_active=0;reset_active=0;console_active=1;retire=1;
        display(7'h40,7'h21,0,0); // ODT microcode is not guest CPU activity
        retire=0;disk_active=1;display(7'h40,7'h21,0,1); // SD still independently visible
        // Reset in every segment/phase, including the active pulse. The
        // external-pin assertions above remain enabled throughout.
        for(i=0;i<64;i=i+1)begin
            power_on=1;@(negedge clk);power_on=0;
            repeat((i+1)*SCAN_DIV)@(negedge clk);
            power_on=1;@(negedge clk);
            if(seg_led_h!==9'h0ff || seg_led_l!==9'h0ff)$fatal(1,"reset during scan did not blank outputs");
        end
        power_on=0;display(7'h40,7'h21,0,1);
        power_on=1;@(negedge clk);
        if(seg_led_h!==9'h0ff || seg_led_l!==9'h0ff)$fatal(1,"power-on reset must blank immediately");
        $display("PASS HC7000 diagnostics: %0d frames/duty checks, 64 reset phases, one-segment invariant, CLOCK_HZ=%0d",checks,CLOCK_HZ);
        $finish;
    end
    initial begin #100000000;$fatal(1,"timeout");end
endmodule
