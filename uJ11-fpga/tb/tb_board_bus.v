`timescale 1ns/1ps
module tb_board_bus;
    reg clk=0,reset=1,peripheral_reset=0,request=0,writing=0,fetch=0,irq_ack=0;
    reg [1:0] lanes=3;reg [15:0] address=0,data=0;
    wire [15:0] value,vector,rom_data;wire [2:0] priority_level;
    wire ack,irq,event_irq,sc,ss,sm,fc,fs,fm,fi,rom_enable;
    wire [8:0] rom_address;wire [5:0] panel;wire host,host_enable,tx;
    reg [15:0] answer;reg [15:0] firmware[0:511];
    integer clocks=0,sd_edges=0,ticks=0,checks=0,guard,previous_edges,i,movb_address=-1;
    always #5 clk=~clk;
    always @(posedge clk)begin clocks=clocks+1;if(event_irq)ticks=ticks+1;end
    always @(posedge ss)sd_edges=sd_edges+1;
    uj11_board_bus #(.TICK_DIVISOR(37),.SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(1)) dut(
        .clk(clk),.rst(reset),.peripheral_reset(peripheral_reset),.request(request),.write(writing),
        .byte_select(lanes),.address(address),.wdata(data),.instruction_fetch(fetch),
        .rdata(value),.acknowledge(ack),.virq(irq),.interrupt_vector(vector),.interrupt_priority(priority_level),
        .interrupt_strobe(irq_ack),.interrupt_acknowledge(),.event_irq(event_irq),.uart_rx(1'b1),.uart_tx(tx),
        .panel_key_rows(4'h3),.panel_din(panel[0]),.panel_ce(panel[1]),.panel_clk(panel[2]),
        .panel_rs(panel[3]),.panel_blank(panel[4]),.panel_reg_latch(panel[5]),
        .host_miso(host),.host_miso_oe(host_enable),.spi_cs_n(fc),.spi_sck(fs),.spi_mosi(fm),.spi_miso(fi),
        .sd_cs_n(sc),.sd_sck(ss),.sd_mosi(sm),.sd_miso(1'b1),
        .boot_rom_ena(rom_enable),.boot_rom_addr(rom_address),.boot_rom_data(rom_data),.boot_complete());
    uj11_firmware_rom rom(clk,rom_enable,rom_address,rom_data);
    spi_fram_model memory(fc,fs,fm,fi);
    task beat(input bit wr,input [1:0] mask,input [15:0] addr,datum,input bit opcode);
        begin
            @(negedge clk);request=1;writing=wr;lanes=mask;address=addr;data=datum;fetch=opcode;guard=0;
            @(posedge clk);
            while(!ack)begin @(posedge clk);guard=guard+1;if(guard>4096)$fatal(1,"bus timeout %o",addr);end
            answer=value;checks=checks+1;
            @(negedge clk);request=0;repeat(2)@(negedge clk);
        end
    endtask
    task accept_irq;
        begin irq_ack=1;@(negedge clk);irq_ack=0;repeat(2)@(negedge clk);end
    endtask
    initial begin
        $readmemh("microcode/generated/firmware.mem",firmware);
        for(i=256;i<512;i=i+1)if(firmware[i][15:12]==9 && movb_address<0)movb_address=16'o160000+(i-256)*2;
        if(movb_address<0)$fatal(1,"firmware MOVB not found");
        repeat(5)@(negedge clk);reset=0;
        beat(0,3,0,0,1);if(answer!=16'o137)$fatal(1,"reset JMP");
        beat(0,3,2,0,0);if(answer!=16'o4000)$fatal(1,"reset target");
        beat(0,3,16'o4000,0,1);if(answer!==firmware[0])$fatal(1,"bootstrap ROM");
        beat(0,3,16'o177750,0,0);if(answer!=16'o31)$fatal(1,"MAINT layout");
        beat(1,2,16'o177751,16'hff00,0);
        beat(0,3,16'o177750,0,0);if(answer!=16'o31)$fatal(1,"MAINT not read-only");
        beat(1,3,16'o166000,16'h5a00,0);
        beat(1,1,16'o166000,16'h00ff,0);
        beat(0,3,16'o166000,0,0);if(answer!=16'h5a30 || {host_enable,host,panel}!=8'h5a)$fatal(1,"panel lanes");
        beat(1,2,16'o166001,16'ha500,0);
        beat(0,3,16'o166000,0,0);if(answer!=16'ha530)$fatal(1,"panel high byte");
        beat(1,1,16'o177546,16'o100,0);repeat(80)@(negedge clk);
        if(!dut.timer_ie || ticks<2)$fatal(1,"KW11 enabled period");
        beat(1,2,16'o177547,16'hff00,0);if(!dut.timer_ie)$fatal(1,"KW11 high byte side effect");
        beat(1,1,16'o177546,0,0);if(dut.timer_ie)$fatal(1,"KW11 disable");
        beat(1,2,16'o177565,16'hff00,0);
        beat(0,3,16'o177564,0,0);if(answer!=16'o200 || irq)$fatal(1,"UART high-byte CSR write");
        beat(1,1,16'o177564,16'o100,0);
        if(!irq || vector!=16'o64 || priority_level!=4)$fatal(1,"UART resolved vector");
        accept_irq();if(irq)$fatal(1,"UART IRQ acknowledge");
        beat(1,1,16'o177564,0,0);
        previous_edges=sd_edges;
        beat(0,2,16'o177501,0,0);if(sd_edges!=previous_edges)$fatal(1,"high SD byte clocked transfer");
        beat(0,1,16'o177500,0,0);if(sd_edges!=previous_edges+8 || answer[7:0]!=255)$fatal(1,"SD read not exactly eight clocks");
        beat(1,3,16'o177442,16'h1234,0);
        beat(1,2,16'o177443,16'hab00,0);
        beat(0,3,16'o177442,0,0);if(answer!=16'hab34)$fatal(1,"RK bank byte write");
        if(memory.memory[65538]!=8'h34 || memory.memory[65539]!=8'hab || memory.memory[2]!=0)$fatal(1,"RK private FRAM bank");
        beat(1,3,16'o177440,16'o101,0);
        if(!irq || vector!=16'o210 || priority_level!=5)$fatal(1,"RK immediate IRQ");
        accept_irq();if(irq)$fatal(1,"RK IRQ acknowledge");
        beat(1,3,16'o177440,16'o2021,0);
        if(!irq || vector!=16'o160000 || priority_level!=7)$fatal(1,"private assist resolved vector");
        accept_irq();if(!dut.rk_service_active)$fatal(1,"private service entry");
        beat(0,3,16'o160000,0,0);if(answer!=16'o160004)$fatal(1,"service vector ROM");
        beat(0,3,movb_address[15:0],0,1);if(!dut.rk_service_movb)$fatal(1,"service MOVB phase");
        beat(1,1,16'o177566,16'h0041,0);
        if(memory.memory[16'o177566]!=8'h41 || !dut.fixed_uart.console.tx_ready)$fatal(1,"DMA hit UART instead of physical FRAM");
        beat(0,3,16'o160476,0,1);if(dut.rk_service_active)$fatal(1,"RTI overlay release");
        $display("PASS board bus: %0d beats; bootstrap/MAINT, byte lanes, KW11, UART/SD side effects, RK bank/vector/DMA/RTI",checks);$finish;
    end
    initial begin #1000000;$fatal(1,"board bus watchdog");end
endmodule
