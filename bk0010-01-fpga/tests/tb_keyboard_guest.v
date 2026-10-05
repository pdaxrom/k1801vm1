`timescale 1ns/1ps
// Physical PS/2 continues while native BK leaves its character latch unread.
module tb_keyboard_guest;
    reg clk = 0, power_on = 1, clock_low = 0, serial_data = 1;
    always #20.833333 clk = !clk;
    tri1 ps2_clock;
    assign ps2_clock = clock_low ? 1'b0 : 1'bz;
    wire [8:0] scan;
    wire pop, valid, key_ready, held, stop, ready, irq;
    wire [7:0] code;
    wire [15:0] result, vector;
    reg request = 0, writing = 0;
    reg [15:0] address = 0, data = 0;
    integer model, key_number;
    uj11_ps2_keyboard #(.CLOCK_HZ(24000000)) transport(clk,power_on,ps2_clock,serial_data,pop,scan);
    bk_keymap map(clk,power_on,scan[8],scan[7:0],pop,valid,code,key_ready,held,stop,1'b0);
    bk_system_bus bus(
        .clk(clk),.power_on(power_on),.reset(1'b0),.peripheral_reset(1'b0),.quiesce(1'b0),
        .request(request),.write(writing),.address(address),.write_data(data),.lanes(2'b11),
        .ready(ready),.read_data(result),.mem_ready(1'b0),.mem_read(16'b0),.boot_data(16'b0),
        .key_valid(valid),.key_code(code),.key_ready(key_ready),.keyboard_irq(irq),
        .keyboard_vector(vector),.irq_ack(1'b0),.sd_miso(1'b1),.map_register(16'b0)
    );
    task bit_send(input value);
        begin serial_data = value; #15000; clock_low = 1; #30000; clock_low = 0; #15000; end
    endtask
    task send(input [7:0] value);
        integer bit_number;
        begin
            wait(ps2_clock); #10000; bit_send(0);
            for(bit_number=0;bit_number<8;bit_number=bit_number+1) bit_send(value[bit_number]);
            bit_send(~^value); bit_send(1); serial_data = 1; #50000;
        end
    endtask
    task release_key(input [7:0] value);
        begin send(8'hf0); send(value); end
    endtask
    task transaction(input write_enable, input [15:0] port, value, expected);
        begin
            @(negedge clk); writing = write_enable; address = port; data = value; request = 1;
            wait(ready);
            if(!write_enable && result !== expected)
                $fatal(1,"Keyboard CSR model=%0d port=%06o got=%06o expected=%06o",model,port,result,expected);
            @(negedge clk); request = 0;
            wait(!ready && bus.state == bus.IDLE);
        end
    endtask
    initial begin
        for(model=0;model<5;model=model+1) begin
            @(negedge clk); power_on = 1;
            repeat(5) @(negedge clk);
            power_on = 0;
            // Adopt a committed model without simulating its independent BIOS.
            bus.model = model; bus.locked = 1;
            transaction(1,16'o177660,0,0);
            send(8'h1c); release_key(8'h1c);
            if(!bus.key_pending || bus.key_latch != "a" || held || !irq || vector != 16'o60)
                $fatal(1,"First native character missing model=%0d",model);
            // Model a player with keyboard IRQ masked and unread data. More
            // than sixteen scan frames catches transport FIFO/inhibit lockup.
            transaction(1,16'o177660,16'o100,0);
            send(8'h12);
            for(key_number=0;key_number<20;key_number=key_number+1) begin
                send(8'h32); release_key(8'h32);
            end
            release_key(8'h12);
            if(bus.key_latch != "a" || !bus.key_pending || held || map.modifiers || scan[8] || transport.inhibit)
                $fatal(1,"Unread register blocked physical key processing model=%0d",model);
            send(8'h76); if(!stop) $fatal(1,"Unread register blocked STOP make model=%0d",model);
            release_key(8'h76); if(stop) $fatal(1,"Unread register blocked STOP break model=%0d",model);
            transaction(0,16'o177660,0,16'o300);
            transaction(0,16'o177662,0,16'h0061);
            transaction(0,16'o177660,0,16'o100);
            #100000;
            if(bus.key_pending || valid || scan[8]) $fatal(1,"Stale characters queued model=%0d",model);
            transaction(1,16'o177660,0,0);
            send(8'he0); send(8'h11); // Right Alt selects keyboard vector 274.
            send(8'h32); release_key(8'h32);
            send(8'he0); release_key(8'h11);
            if(!bus.key_pending || bus.key_latch != 8'he2 || held || !irq || vector != 16'o274)
                $fatal(1,"Keyboard did not resume after reading register model=%0d",model);
            transaction(0,16'o177662,0,16'h0062);
            $display("PASS model=%0d: occupied keyboard CSR, PS/2 burst, STOP/breaks, no stale keys, IRQ274 recovery",model);
        end
        $finish;
    end
    initial begin #300000000; $fatal(1,"Guest keyboard timeout model=%0d",model); end
endmodule
