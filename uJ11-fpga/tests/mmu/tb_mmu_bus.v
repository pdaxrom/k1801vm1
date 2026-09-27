`timescale 1ns/1ps
module tb_mmu_bus;
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
    wire tx,cs,sck,mosi;wire [7:0] pins;
    integer checks=0,n;
    uj11_mmu_board_bus #(.CLEAR_WORDS(1),.TICK_DIVISOR(1024)) dut(
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),
        .request(request),.writing(writing),.byte_access(byte_access),.address(address),.write_data(write_data),
        .ready(ready),.error(error),.read_data(read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.uart_rx(1'b1),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(1'b1),.boot_complete(boot_complete));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s address=%h data=%h",why,address,read_data);checks++;end
    endtask
    reg [15:0] value;integer writes_before;
    task bus(input bit wr,input bit by,input [21:0] addr,input [15:0] data,input bit fault);
        begin
            @(negedge clk);request=1;writing=wr;byte_access=by;address=addr;write_data=data;n=0;
            do begin @(posedge clk);n++;if(n>1024)$fatal(1,"bus timeout");end while(!ready);
            check(error==fault,"physical response");value=read_data;
            @(negedge clk);request=0;repeat(3)@(negedge clk);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        bus(1,0,0,16'hbeef,0);bus(1,0,22'h10000,16'h1234,0);
        bus(1,0,22'h100000,16'h5678,0);bus(1,0,22'h1ffffe,16'habcd,0);
        bus(0,0,0,0,0);check(value===16'hbeef,"low RAM");
        bus(0,0,22'h10000,0,0);check(value===16'h1234,"64 KiB RAM");
        bus(0,0,22'h100000,0,0);check(value===16'h5678,"1 MiB RAM");
        bus(0,0,22'h1ffffe,0,0);check(value===16'habcd,"last SRAM word");
        bus(1,1,22'h1fffff,16'h5500,0);bus(1,1,22'h1ffffe,16'h00aa,0);
        bus(0,0,22'h1ffffe,0,0);check(value===16'h55aa,"top byte lanes");
        writes_before=ram.writes;
        bus(1,0,22'h200000,16'hdead,1);bus(1,0,22'h300000,16'hdead,1);
        bus(1,0,22'h3e0000,16'hdead,1);bus(0,0,22'h3ffff0,0,1);
        check(ram.writes==writes_before,"NXM cannot alias SRAM");
        bus(0,0,0,0,0);check(value===16'hbeef,"NXM leaves low RAM intact");
        bus(1,0,22'o177546,16'ha55a,0);bus(0,0,22'o177546,0,0);
        check(value===16'ha55a && !irq_valid,"low 64 KiB IO-looking address is SRAM");
        bus(0,0,22'o17777546,0,0);check(value===16'o200,"canonical timer CSR");
        bus(1,0,22'o17777546,16'o300,0);check(irq_valid && irq_priority==6 && irq_vector==16'o100,"timer interrupt");
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
        bus(1,0,22'o17777546,0,0);check(!irq_valid,"timer disable");
        bus(1,0,22'o17766000,16'h4100,0);check(pins===8'h41,"HG/panel output");
        bus(1,0,22'o4000,16'hcafe,0);bus(0,0,22'o4000,0,0);
        check(value===16'o12706,"ROM overlays underlying RAM");
        bus(1,0,22'o17777502,16'd7,0);bus(0,0,0,0,0);check(boot_complete,"bootstrap release");
        bus(0,0,22'o4000,0,0);check(value===16'hcafe,"released ROM exposes all guest RAM");
        @(negedge clk);peripheral_reset=1;@(negedge clk);peripheral_reset=0;
        bus(0,0,22'o4000,0,0);check(value===16'hcafe && boot_complete,"RESET does not re-overlay boot code");
        @(negedge clk);reset=1;repeat(3)@(negedge clk);reset=0;
        bus(0,0,22'o4000,0,0);check(value===16'o12706 && !boot_complete,"hard reset restores boot ROM");
        bus(0,0,22'h1ffffe,0,0);check(value===16'h55aa,"hard reset preserves SRAM");
        $display("PASS MMU board bus: %0d checks",checks);$finish;
    end
    initial begin #10000000;$fatal(1,"global timeout");end
endmodule
