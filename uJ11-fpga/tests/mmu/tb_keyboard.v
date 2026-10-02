`timescale 1ns/1ps
// Physical PS/2 and UART wires -> real SERV firmware -> DL11 CSR/RBUF/IRQ.
module tb_keyboard;
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,irq_ack=0,rx=1,device_clock=0,ps2_data=1;
    tri1 ps2_clock;
    assign ps2_clock=device_clock ? 1'b0 : 1'bz;
    reg [21:0] address=0;
    reg [15:0] write_data=0;
    wire ready,error,initialized,cpu_start,irq_valid;
    wire [2:0] irq_priority;
    wire [15:0] read_data,irq_vector;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_mmu_board_bus #(.CLOCK_HZ(50000000),.CLEAR_WORDS(1),.BOOT_ROM_ENABLE(0),
        .VIDEO_ENABLE(1),.TERMINAL_ENABLE(1),.KEYBOARD_ENABLE(1)) bus(
        .clk(clk),.video_clk(vclk),.video_reset(1'b0),.reset(reset),.power_on(power_on),
        .peripheral_reset(peripheral_reset),.dma_map_enabled(1'b0),.tvout(),
        .ps2_clock(ps2_clock),.ps2_data(ps2_data),
        .request(request),.writing(writing),.byte_access(1'b0),.cpu_lock(1'b0),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector),.irq_ack(irq_ack),
        .uart_rx(rx),.uart_tx(),.panel_keys(4'b0),.panel_pins(),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),
        .sram_lb_n(lb),.sram_ub_n(ub),.sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),
        .boot_complete(),.cpu_start(cpu_start));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    serv_memory_guard guard(.clk(clk),.reset(bus.disk.iop_reset),
        .write(bus.disk.data_accept && bus.disk.memory_selected && bus.disk.de),.address(bus.disk.da));
    integer checks=0,bytes=0,inhibited=0;
    reg [15:0] result;
    always @(posedge clk)if(!reset && bus.disk.keyboard.receiver.inhibit)inhibited++;
    task access(input [21:0] a,input bit w,input [15:0] d);
        @(negedge clk);address=a;writing=w;write_data=d;request=1;
        do @(negedge clk);while(!ready);
        if(error)$fatal(1,"DL11 bus error");
        result=read_data;request=0;
        repeat(2)@(negedge clk);
    endtask
    task get(input [7:0] expected);
        integer n;
        n=0;
        do begin
            access(22'o17777560,0,0);n++;
            if(n>1000000)$fatal(1,"no keyboard character %x",expected);
        end while(!result[7]);
        if(result[15] || result[12])$fatal(1,"keyboard set UART error flags");
        if(!irq_valid || irq_priority!=4 || irq_vector!=16'o60)$fatal(1,"keyboard RX IRQ/vector");
        irq_ack=1;@(negedge clk);irq_ack=0;
        access(22'o17777562,0,0);
        if(result!={8'b0,expected})$fatal(1,"RBUF=%x expected=%x",result,expected);
        checks++;bytes++;
    endtask
    task ps2_bit(input bit b);
        ps2_data=b;#7000;device_clock=1;#25000;device_clock=0;#28000;
    endtask
    task scan(input [7:0] b);
        wait(ps2_clock);#7000;ps2_bit(0);
        for(integer i=0;i<8;i++)ps2_bit(b[i]);
        ps2_bit(~^b);ps2_bit(1);ps2_data=1;#10000;
    endtask
    task serial(input [7:0] b);
        rx=0;repeat(434)@(negedge clk);
        for(integer i=0;i<8;i++)begin rx=b[i];repeat(434)@(negedge clk);end
        rx=1;repeat(434)@(negedge clk);
    endtask
    task text(input string s);
        for(integer i=0;i<s.len();i++)begin
            do access(22'o17777564,0,0);while(!result[7]);
            access(22'o17777566,1,s[i]);
        end
        // Wait until real SERV consumes the final mode-selecting byte.
        wait(bus.disk.terminal.fifo.level==0);repeat(50000)@(negedge clk);
    endtask
    initial begin
        for(integer i=0;i<2097152;i++)ram.memory[i]=8'h5a;
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait(cpu_start);access(22'o17777560,1,16'h40);
        scan(8'haa);scan(8'h1c);get("a");
        scan(8'hf0);scan(8'h1c);scan(8'h12);scan(8'h32);get("B");
        scan(8'hf0);scan(8'h12);scan(8'h14);scan(8'h21);get(3);
        scan(8'hf0);scan(8'h14);scan(8'h5a);get(13);
        scan(8'he0);scan(8'h75);get(27);get("[");get("A");
        text("\033[?2l");scan(8'he0);scan(8'h6b);get(27);get("D");
        text("\033<\033[?1h");scan(8'he0);scan(8'h74);get(27);get("O");get("C");
        // UART receives first; pending keyboard input cannot overwrite its RBUF.
        fork
            scan(8'h1c);
            begin #600000;serial("Z");end
        join
        repeat(200000)@(negedge clk);
        get("Z");get("a");
        // Fill the raw queue while RBUF is occupied, then resume consumption.
        fork
            begin for(integer i=0;i<24;i++)scan(8'h1c);end
            begin #20000000;for(integer i=0;i<24;i++)get("a");end
        join
        if(!inhibited)$fatal(1,"no physical PS2 backpressure exercised");
        scan(8'h12);scan(8'h1c);
        repeat(200000)@(negedge clk);
        peripheral_reset=1;repeat(5)@(negedge clk);peripheral_reset=0;
        repeat(200000)@(negedge clk);
        access(22'o17777560,0,0);if(result[7])$fatal(1,"guest RESET retained input");
        access(22'o17777560,1,16'h40);scan(8'h1c);get("a");
        $display("PASS SERV PS2/DL11/UART: checks=%0d bytes=%0d inhibited=%0d",checks,bytes,inhibited);
        $finish;
    end
    initial begin #500000000;$fatal(1,"keyboard integration timeout");end
endmodule
