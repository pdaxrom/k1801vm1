`timescale 1ns/1ps
// Guest RESET must retain video latches and permit the next IRQ100.
module tb_reset;
    reg clk = 0, power_on = 1, reset = 0, peripheral_reset = 0;
    always #500 clk = !clk;
    reg request = 0;
    reg [15:0] address = 0, data = 0;
    reg [1:0] lanes = 3;
    integer m, l;
    reg [15:0] expected_video;
    wire ready, irq;
    wire [15:0] vector, scroll, video;
    bk_system_bus #(.CLOCK_HZ(1000000)) bus (
        .clk(clk),.power_on(power_on),.reset(reset),.peripheral_reset(peripheral_reset),.quiesce(1'b0),
        .request(request),.write(1'b1),.address(address),.write_data(data),.lanes(lanes),.ready(ready),
        .mem_ready(1'b0),.mem_read(16'b0),.boot_data(16'b0),.key_valid(1'b0),.key_code(8'b0),
        .keyboard_irq(irq),.keyboard_vector(vector),.irq_ack(1'b0),.scroll(scroll),
        .sd_miso(1'b1),.map_register(16'b0),.video_register(video)
    );
    task put(input [15:0] port, value);
        begin
            @(negedge clk); address = port; data = value; request = 1;
            wait(ready || bus.state == bus.UNMAPPED);
            @(negedge clk); request = 0;
            wait(!ready && bus.state == bus.IDLE);
        end
    endtask
    initial begin
        repeat(5) @(negedge clk);
        power_on = 0;
        // Fixture adopts the committed BK11 configuration; no BIOS bypass in
        // the separate native disk/music cold-boot integration test.
        bus.model = 3; bus.locked = 1;
        put(16'o177662,16'o105400); // Page 6, palette 11, frame IRQ enabled.
        put(16'o177664,16'o001336);
        wait(irq);
        if(vector != 16'o100) $fatal(1,"Missing initial frame IRQ");
        @(negedge clk); peripheral_reset = 1;
        repeat(3) @(negedge clk);
        if(irq || bus.frame_pending || video != 16'o105400 || scroll != 16'o001336)
            $fatal(1,"INIT changed video state or retained pending IRQ");
        peripheral_reset = 0;
        wait(irq);
        if(vector != 16'o100 || video != 16'o105400 || scroll != 16'o001336)
            $fatal(1,"Frame IRQ did not resume after guest RESET");
        @(negedge clk); reset = 1;
        repeat(3) @(negedge clk);
        if(irq || video != 16'o040000 || scroll != 16'o001330)
            $fatal(1,"Hardware reset did not restore startup video state");
        for(m=0;m<5;m=m+1) for(l=0;l<2;l=l+1) begin
            // Exercise the extracted latches through real CSR transactions,
            // including byte writes and the BK10 read-only keyboard alias.
            reset = 0; bus.model = m; bus.locked = l;
            lanes = 1;
            put(16'o177662,16'hffff);
            if(video != 16'o040000) $fatal(1,"Low byte changed video");
            lanes = 2;
            put(16'o177663,16'hab00);
            expected_video = m >= 3 && l ? 16'h8b00 : 16'o040000;
            if(video != expected_video) $fatal(1,"Video CSR decode model=%0d locked=%0d",m,l);
            lanes = 1;
            put(16'o177664,16'h00c3);
            if(scroll != 16'h02c3) $fatal(1,"Low scroll byte");
            lanes = 2;
            put(16'o177665,16'ha500);
            if(scroll != 16'h00c3) $fatal(1,"High scroll mask");
            @(negedge clk); peripheral_reset = 1;
            repeat(3) @(negedge clk);
            if(scroll != 16'h00c3 || video != expected_video)
                $fatal(1,"Guest RESET changed video latches model=%0d locked=%0d",m,l);
            peripheral_reset = 0; reset = 1;
            repeat(3) @(negedge clk);
        end
        $display("PASS guest RESET preserves video/scroll, withdraws IRQ and resumes IRQ100; hardware reset restores defaults");
        $display("PASS video/scroll byte CSR masks and model/lock gates for all five models");
        $finish;
    end
    initial begin #100000000; $fatal(1,"RESET regression timeout"); end
endmodule
