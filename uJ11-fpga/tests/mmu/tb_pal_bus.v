`timescale 1ns/1ps
// Real SERV instruction execution: PDP IO -> firmware -> PAL control MMIO.
module tb_pal_bus;
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;reg [15:0] write_data=0;
    wire ready,error,initialized;wire [15:0] read_data;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire [5:0] tvout;
    uj11_mmu_board_bus #(.CLOCK_HZ(50000000),.CLEAR_WORDS(1),.VIDEO_ENABLE(1)) dut(
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),
        .dma_map_enabled(1'b0),.video_clk(vclk),.video_reset(1'b0),.tvout(tvout),
        .request(request),.writing(writing),.byte_access(byte_access),.cpu_lock(1'b0),
        .address(address),.write_data(write_data),.ready(ready),.error(error),.read_data(read_data),
        .irq_valid(),.irq_priority(),.irq_vector(),.irq_ack(1'b0),.uart_rx(1'b1),.uart_tx(),
        .panel_keys(4'b0),.panel_pins(),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.cpu_start());
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    serv_memory_guard guard(.clk(clk),.reset(reset),
        .write(dut.disk.data_accept && dut.disk.memory_selected && dut.disk.de),.address(dut.disk.da));
    integer n,checks=0;reg [15:0] value;
    task bus(input bit wr,input bit by,input [21:0] a,input [15:0] d,input bit fault);
        begin
            @(negedge clk);request=1;writing=wr;byte_access=by;address=a;write_data=d;n=0;
            do begin @(posedge clk);n++;if(n>200000)$fatal(1,"SERV timeout %o",a);end while(!ready);
            if(error!=fault)$fatal(1,"SERV NXM %o",a);
            value=read_data;checks++;
            @(negedge clk);request=0;repeat(5)@(negedge clk);
        end
    endtask
    task expect_value(input [15:0] want);
        begin if(value!==want)$fatal(1,"CSR %o got %o expected %o",address,value,want);end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        bus(0,0,22'o17777200,0,0);expect_value(0);
        bus(1,0,22'o17777202,16'h1234,0);
        bus(1,1,22'o17777203,16'hab00,0);
        bus(1,1,22'o17777202,16'h00cd,0);
        bus(0,0,22'o17777202,0,0);expect_value(16'habcd);
        bus(1,0,22'o17777202,0,0);bus(1,0,22'o17777204,16'h1e,0);
        bus(1,0,22'o17777200,5,0); // Safe colour test: no framebuffer DMA.
        bus(1,0,22'o17777210,1,0);
        bus(0,0,22'o17777210,0,0);expect_value(1);
        bus(1,0,22'o17777200,0,0); // Busy: cannot corrupt held config mailbox.
        bus(0,0,22'o17777200,0,0);expect_value(5);
        wait(dut.pal.video.vcontrol==5);wait(!dut.pal.video.config_busy);
        bus(0,0,22'o17777210,0,0);expect_value(0);
        if(dut.pal.video.dma_request)$fatal(1,"Pattern requested guest RAM");
        bus(0,0,22'o17777220,0,1); // No expansion of the hardware CSR decoder.
        @(negedge clk);peripheral_reset=1;repeat(2)@(negedge clk);peripheral_reset=0;
        repeat(100)@(negedge clk);
        bus(0,0,22'o17777200,0,0);expect_value(0);
        $display("PASS PAL SERV bus: %0d checks",checks);$finish;
    end
    initial begin #65000000;$fatal(1,"PAL SERV global timeout");end
endmodule
