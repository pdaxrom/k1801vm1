`timescale 1ns/1ps
module tb_storage_bus;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,power_on=1,peripheral_reset=0;
    always #20.833 clk=~clk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;reg [15:0] write_data=0;
    wire ready,error,initialized,boot_complete,irq_valid;
    wire [15:0] read_data,irq_vector;
    wire [2:0] irq_priority;
    reg irq_ack=0;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire tx,cs,sck,mosi,miso;wire [7:0] pins;
    integer checks=0,n;
    uj11_mmu_board_bus #(.CLEAR_WORDS(1),.TICK_DIVISOR(1024),.CLOCK_HZ(240000),.SD_SLOW_DIV(4),.SD_FAST_DIV(2)) dut(
        .ps2_clock(),.ps2_data(1'b1),
        .video_clk(1'b0),.video_reset(1'b1),.tvout(),
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),.cpu_lock(1'b0),.dma_map_enabled(1'b0),
        .request(request),.writing(writing),.byte_access(byte_access),.address(address),.write_data(write_data),
        .ready(ready),.error(error),.read_data(read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.uart_rx(1'b1),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete),.cpu_start());
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s address=%h data=%h",why,address,read_data);checks++;end
    endtask
    reg [15:0] value;integer writes_before;
    task bus(input bit wr,input bit by,input [21:0] addr,input [15:0] data,input bit fault);
        begin
            @(negedge clk);request=1;writing=wr;byte_access=by;address=addr;write_data=data;n=0;
            do begin @(posedge clk);n++;if(n>10000000)$fatal(1,"bus timeout");end while(!ready);
            check(error==fault,"physical response");value=read_data;
            @(negedge clk);request=0;repeat(3)@(negedge clk);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        do bus(0,0,22'o17777504,0,0);while(!value[15]);
        wait(!dut.disk.owner);
        bus(0,0,22'o17774400,0,0);check(value==16'o201,"RL canonical CSR");
        bus(0,0,22'o17777400,0,1); // RK05 must not alias RL11.
        bus(0,0,22'o17777440,0,1); // No RH partitions.
        bus(0,0,22'o17776726,0,0);check(value==16'o20027,"RM05 type register");
        bus(1,0,22'o17774400,16'o300,0);
        bus(1,0,22'o17776700,16'o100,0);
        check(irq_valid && irq_priority==5 && irq_vector==16'o160,"RL takes first BR5 vector");
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
        wait(irq_valid);check(irq_valid && irq_vector==16'o254,"RL acknowledge preserves pending XP");
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;check(!irq_valid,"XP acknowledge");
        @(negedge clk);peripheral_reset=1;@(negedge clk);peripheral_reset=0;
        bus(0,0,22'o17774400,0,0);check(value==16'o201,"RL stays attached across PDP RESET");
        bus(0,0,22'o17776726,0,0);check(value==16'o20027,"XP stays attached across PDP RESET");
        $display("PASS MMU board bus: %0d checks",checks);$finish;
    end
    initial begin #1000000000;$fatal(1,"global timeout");end
endmodule
