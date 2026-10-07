`timescale 1ns/1ps
// Exercise the actual native bus/video CDC and interrupt latch at 50/64 MHz.
module tb_terak_bevnt;
    reg clk=0,vclk=0,video_running=1,reset=1,power_on=1,peripheral_reset=0,video_reset=0,irq_ack=0;
    always #10 clk=~clk;
    always #7.8125 if(video_running)vclk=~vclk;
    wire irq_valid;
    wire [2:0] irq_priority;
    wire [15:0] irq_vector;
    wire [19:0] sa;
    wire [15:0] sd;
    wire ce,oe,we,lb,ub,initialized;
    tri1 ps2_clock;
    uj11_terak_bus #(.CLEAR_WORDS(1)) bus(.clk(clk),.reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.video_clk(vclk),.video_reset(video_reset),
        .request(1'b0),.writing(1'b0),.byte_access(1'b0),.cpu_lock(1'b0),.address(22'b0),.write_data(16'b0),
        .ready(),.error(),.read_data(),.irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),
        .irq_ack(irq_ack),.ps2_clock(ps2_clock),.ps2_data(1'b1),.tvout(),.audio(),
        .memory_initialized(initialized),.cpu_start(),.boot_complete(),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    integer fields=0,events=0;
    realtime field_time;
    always @(posedge vclk)if(!reset && !video_reset && !bus.video.vrst && bus.video.field_start)begin
        fields++;field_time=$realtime;
    end
    always @(posedge clk)if(!reset && !video_reset && !bus.event_reset[2] && bus.tick)begin
        events++;
        if(events!=fields || $realtime-field_time>100 || $realtime-field_time<20)
            $fatal(1,"field/BEVNT mismatch fields=%0d events=%0d delay=%0f",fields,events,$realtime-field_time);
        #1;
        if(!peripheral_reset && (!irq_valid || irq_priority!=6 || irq_vector!=16'o100))
            $fatal(1,"BEVNT vector/priority");
    end
    task acknowledge;
        @(negedge clk);irq_ack=1;
        @(negedge clk);irq_ack=0;
        if(bus.clock_pending)$fatal(1,"BEVNT acknowledgment");
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait(events==1);repeat(5)@(negedge clk);
        if(bus.vcr!=0)$fatal(1,"test requires blanked VCR");
        acknowledge();
        wait(events==2);repeat(5)@(negedge clk);
        // An unacknowledged event coalesces, then ACK clears only that latch.
        wait(events==3);repeat(5)@(negedge clk);
        if(!bus.clock_pending)$fatal(1,"lost pending BEVNT");
        acknowledge();
        // Peripheral RESET must track fields without restarting their phase.
        peripheral_reset=1;
        wait(events==4);repeat(5)@(negedge clk);
        if(bus.clock_pending)$fatal(1,"BEVNT survived peripheral RESET");
        peripheral_reset=0;
        repeat(20)@(negedge clk);
        if(bus.clock_pending)$fatal(1,"stale event after peripheral RESET");
        wait(events==5);repeat(5)@(negedge clk);
        // PLL/reset asserted while video is stopped must clear CDC immediately.
        @(negedge vclk);video_running=0;video_reset=1;
        repeat(10)@(negedge clk);
        if(bus.clock_pending || bus.field_sync || bus.video.field_toggle)$fatal(1,"paused video reset");
        fields=0;events=0;video_reset=0;
        repeat(20)@(negedge clk);
        if(bus.clock_pending || events)$fatal(1,"phantom event with video clock stopped");
        video_running=1;
        wait(events==1);repeat(5)@(negedge clk);acknowledge();
        wait(events==2);repeat(5)@(negedge clk);
        $display("PASS video-field BEVNT vector 100/BR6, CDC latency, blank VCR, ACK/coalescing and resets");
        $finish;
    end
    initial begin #100000000;$fatal(1,"BEVNT timeout");end
endmodule
