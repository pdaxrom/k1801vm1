`timescale 1ns/1ps
// Real native bus, serial pins, IRQ arbitration and 50/64 MHz clocks.
module tb_terak_uart #(parameter DISPLAY_EBR=1,RAM_BYTES=2097152,UART_BAUD=19200);
    localparam BIT_TICKS=(50000000+UART_BAUD/2)/UART_BAUD;
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0,irq_ack=0,rx=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;
    reg [15:0] write_data=0;
    wire ready,error,irq_valid,tx,initialized;
    wire [15:0] read_data,irq_vector;
    wire [2:0] irq_priority;
    wire [19:0] sa;
    wire [15:0] sd;
    wire ce,oe,we,lb,ub;
    tri1 ps2_clock;
    uj11_terak_bus #(.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.CLEAR_WORDS(1),
        .BOOT_BIOS(1),.RL_ENABLE(1),.UART_BAUD(UART_BAUD)) bus(
        .clk(clk),.reset(reset),.cold_reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.video_clk(vclk),.video_reset(1'b0),.uart_rx(rx),.uart_tx(tx),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(1'b0),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .ps2_clock(ps2_clock),.ps2_data(1'b1),.tvout(),.audio(),.memory_initialized(initialized),
        .cpu_start(),.storage_idle(),.boot_complete(),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));

    task cycles(input integer n);
        repeat(n)@(negedge clk);
    endtask
    task start_access(input [15:0] a,input wr,input by,input [15:0] d);
        @(negedge clk);address=22'h3f0000 | a;writing=wr;byte_access=by;write_data=d;request=1;
        @(negedge clk);
        while(!ready)@(negedge clk);
        if(error)$fatal(1,"UART bus error at %o",a);
    endtask
    task finish_access;
        // Models the BIOS board's additional registered response stage.
        cycles(6);request=0;cycles(2);
    endtask
    task put(input [15:0] a,input by,input [15:0] d);
        start_access(a,1,by,d);finish_access;
    endtask
    task check(input [15:0] a,input [15:0] mask,input [15:0] expected);
        start_access(a,0,0,0);
        if((read_data & mask)!==expected)$fatal(1,"register %o = %o, expected %o mask %o",a,read_data,expected,mask);
        finish_access;
    endtask
    task send(input [7:0] value,input stop);
        integer i;
        @(negedge clk);rx=0;cycles(BIT_TICKS);
        for(i=0;i<8;i=i+1)begin rx=value[i];cycles(BIT_TICKS);end
        rx=stop;cycles(BIT_TICKS);rx=1;cycles(BIT_TICKS);
    endtask
    task expect_tx(input [7:0] value);
        integer i;
        @(negedge tx);cycles(BIT_TICKS/2);
        if(tx!==0)$fatal(1,"missing TX start");
        for(i=0;i<8;i=i+1)begin
            cycles(BIT_TICKS);
            if(tx!==value[i])$fatal(1,"TX bit %0d",i);
            if(bus.serial.tx_done || bus.uart_tx_irq)$fatal(1,"TX DONE/IRQ before complete character");
        end
        cycles(BIT_TICKS);
        if(tx!==1 || bus.serial.tx_done)$fatal(1,"TX stop must occupy a whole bit");
        cycles(BIT_TICKS/2+10);
    endtask
    task interrupt(input [15:0] vector);
        cycles(5);
        if(!irq_valid || irq_priority!=4 || irq_vector!=vector)$fatal(1,"IRQ vector %o expected %o",irq_vector,vector);
        irq_ack=1;cycles(1);irq_ack=0;cycles(3);
    endtask
    initial begin
        cycles(5);power_on=0;wait(initialized);@(negedge clk);reset=0;cycles(20);
        // Video begins its first field immediately after reset.
        if(bus.clock_pending)begin irq_ack=1;cycles(1);irq_ack=0;cycles(3);end
        if(tx!==1)$fatal(1,"TX reset level");
        check(16'o177520,16'hffff,0);
        check(16'o177524,16'hffff,16'h8080);
        // Upper-byte writes cannot change IE, BREAK or data.
        put(16'o177525,1,16'hffff);check(16'o177524,16'hffff,16'h8080);
        put(16'o177527,1,16'hffff);cycles(10);
        if(tx!==1)$fatal(1,"odd XDB write transmitted");
        put(16'o177524,0,16'h0040);interrupt(16'o124);
        if(irq_valid)$fatal(1,"TX ACK repeated without a new DONE transition");
        fork
            put(16'o177526,1,16'h00a5);
            expect_tx(8'ha5);
        join
        check(16'o177524,16'hffff,16'h80c0);interrupt(16'o124);
        put(16'o177520,1,16'h0040);
        send(8'h3c,1);check(16'o177520,16'hffff,16'h00c0);interrupt(16'o120);
        if(irq_valid)$fatal(1,"RX ACK did not clear request latch");
        check(16'o177522,16'hffff,16'h003c);check(16'o177520,16'hffff,16'h0040);
        send(8'h41,1);send(8'h42,1);
        check(16'o177520,16'hffff,16'h90c0);
        check(16'o177522,16'hffff,16'h0042);check(16'o177520,16'hffff,16'h0040);
        send(8'h00,0);check(16'o177520,16'ha0c0,16'ha0c0);
        check(16'o177522,16'hffff,0);check(16'o177520,16'hffff,16'h0040);
        // IE enabled after reception must immediately request an interrupt.
        put(16'o177520,0,0);send(8'h55,1);
        if(irq_valid)$fatal(1,"RX interrupt with IE disabled");
        put(16'o177521,1,16'hffff);check(16'o177520,16'hffff,16'h0080);
        put(16'o177520,0,16'h0040);interrupt(16'o120);
        // RDB reads have one side effect even while held over a new RX frame.
        start_access(16'o177522,0,1,0);
        if(read_data!==16'h0055)$fatal(1,"RDB byte read");
        send(8'h66,1);finish_access;
        check(16'o177520,16'hffff,16'h00c0);
        check(16'o177522,16'hffff,16'h0066);
        put(16'o177524,0,16'h0041);cycles(5);
        if(tx!==0)$fatal(1,"TX BREAK pin");
        check(16'o177524,16'hffff,16'h80c0);
        put(16'o177524,0,16'h0040);cycles(5);
        if(tx!==1)$fatal(1,"TX BREAK release");
        // Clock BR6 wins over serial BR4 and its ACK leaves serial pending.
        put(16'o177524,0,0);put(16'o177524,0,16'h0040);
        wait(bus.clock_pending);@(negedge clk);
        if(irq_priority!=6 || irq_vector!=16'o100)$fatal(1,"BEVNT priority");
        irq_ack=1;cycles(1);irq_ack=0;interrupt(16'o124);
        // Simultaneous RX/TX requests: receive vector first, then transmit.
        put(16'o177524,0,0);put(16'o177524,0,16'h0040);send(8'h77,1);
        interrupt(16'o120);interrupt(16'o124);
        // INIT cancels pending RX/TX, BREAK and enabled interrupts.
        put(16'o177524,0,16'h0041);peripheral_reset=1;cycles(3);peripheral_reset=0;cycles(5);
        check(16'o177520,16'hffff,0);check(16'o177524,16'hffff,16'h8080);
        if(tx!==1 || irq_valid)$fatal(1,"INIT UART reset");
        // Guest reset also restores the physical UART.
        put(16'o177524,0,1);reset=1;cycles(3);reset=0;cycles(5);
        check(16'o177524,16'hffff,16'h8080);
        // Reject a short low glitch, then receive all byte values at the
        // nominal wire rate, independently of the internal 16x divider phase.
        rx=0;cycles(BIT_TICKS/8);rx=1;cycles(BIT_TICKS*2);
        check(16'o177520,16'hffff,0);
        for(integer value=0;value<256;value=value+1)begin
            send(value[7:0],1);
            check(16'o177520,16'hffff,16'h0080);
            check(16'o177522,16'hffff,value);
        end
        $display("PASS TERAK UART unit 1 %0d 8N1 pins, byte lanes, held cycles, errors, 120/124 IRQs and RESET",UART_BAUD);
        $finish;
    end
    initial begin #250000000;$fatal(1,"UART timeout");end
endmodule
