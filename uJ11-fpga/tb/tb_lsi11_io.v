`timescale 1ns/1ps
// Uses the actual, frozen lsi11-fpga bus/UART/SD RTL, not a CSR oracle.
// Only its I/O port is exercised; its FRAM/boot/RK paths are not uJ11 hardware.
module tb_lsi11_io;
    reg clk=0,reset=1,request=0,write=0,byte_access=0,stream=0;
    reg [15:0] address=0,wdata=0;
    wire [15:0] rdata,io_address,io_wdata,io_rdata,legacy_data;
    wire ack,error,io_request,io_write,io_byte,io_ack,cs_n,sck,mosi,miso;
    reg uart_rx=1;
    wire uart_tx,virq,event_irq,sd_cs_n,sd_sck,sd_mosi,legacy_cs,legacy_sck;
    wire [15:0] vector;
    wire [1:0] lanes=io_byte ? (io_address[0] ? 2'b10 : 2'b01) : 2'b11;
    wire [15:0] lane_data=io_byte && io_address[0] ? {io_wdata[7:0],8'b0} : io_wdata;
    assign io_rdata=io_byte ? (io_address[0] ? {8'b0,legacy_data[15:8]} : {8'b0,legacy_data[7:0]}) : legacy_data;
    integer edges=0,accepted=0,ticks=0,c,beats=0,old_edges,old_accepted;
    reg [15:0] got;
    always #17 clk=~clk;
    always @(posedge sd_sck) begin
        edges=edges+1;
        if(!sd_mosi) $fatal(1,"SD read must send FF");
    end
    always @(posedge clk) begin
        if(legacy_sck || !legacy_cs) $fatal(1,"I/O adapter fell through to its private FRAM");
        if(io_request && io_ack) accepted=accepted+1;
        if(event_irq) ticks=ticks+1;
    end
    uj11_prefetch memory(.prefetch_enable(1'b1),.clk(clk),.reset(reset),.request(request),.write(write),.byte_access(byte_access),
        .stream(stream),.address(address),.wdata(wdata),.pc_write(1'b0),.pc_data(16'b0),.stopped(1'b0),
        .rdata(rdata),.ack(ack),.error(error),.io_request(io_request),.io_write(io_write),.io_byte(io_byte),
        .io_address(io_address),.io_wdata(io_wdata),.io_rdata(io_rdata),.io_ack(io_ack),.io_error(1'b0),
        .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    am4_hc1200_cpu11_bus #(.CLOCK_HZ(1843200),.TICK_DIVISOR(400),
        .BOOT_ROM_ENABLE(0),.SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(0),.SD_SLOW_DIV(2),.SD_FAST_DIV(1)) legacy(
        .clk(clk),.rst(reset),.peripheral_reset(1'b0),.request(io_request),.write(io_write),
        .byte_select(lanes),.address(io_address),.wdata(lane_data),.instruction_fetch(stream),
        .rdata(legacy_data),.acknowledge(io_ack),.virq(virq),.interrupt_vector(vector),
        .interrupt_strobe(1'b0),.interrupt_acknowledge(),.event_irq(event_irq),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .panel_key_rows(4'ha),.panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_reg_latch(),
        .spi_cs_n(legacy_cs),.spi_sck(legacy_sck),.spi_mosi(),.spi_miso(1'b0),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(1'b1),
        .boot_rom_ena(),.boot_rom_addr(),.boot_rom_data(8'b0),.boot_complete(),.host_miso(),.host_miso_oe());
    task tick; begin @(posedge clk); #1; end endtask
    task beat;
        input wr,bt,st; input [15:0] at,dat;
        begin
            @(negedge clk); request=1; write=wr; byte_access=bt; stream=st; address=at; wdata=dat;
            c=0;
            begin : waiting
                forever begin @(posedge clk); c=c+1;
                    if(ack) begin got=rdata; if(error) $fatal(1,"I/O error"); #1; disable waiting; end
                    if(c>300) $fatal(1,"I/O timeout %o",at);
                    #1;
                end
            end
            @(negedge clk); request=0; beats=beats+1;
            // LSI11 UART and SD require a sampled low strobe between beats.
            tick;
        end
    endtask
    task send_uart;
        input [7:0] value; integer b;
        begin
            @(negedge clk); uart_rx=0; repeat(16) tick;
            for(b=0;b<8;b=b+1) begin @(negedge clk); uart_rx=value[b]; repeat(16) tick; end
            @(negedge clk); uart_rx=1; repeat(24) tick;
        end
    endtask
    initial begin
        #100; tick; @(negedge clk); reset=0;
        beat(0,0,0,16'o166000,0); if(got!==16'h12a0) $fatal(1,"panel reset/rows");
        beat(1,1,0,16'o166000,16'hff); beat(0,0,0,16'o166000,0);
        if(got!==16'h12a0) $fatal(1,"panel low-byte write");
        beat(1,1,0,16'o166001,16'ha5); beat(0,1,0,16'o166001,0);
        if(got!==16'ha5) $fatal(1,"panel high lane mapping");
        beat(0,0,0,16'o177546,0); if(got!==16'h80) $fatal(1,"KW11 reset");
        beat(1,1,0,16'o177547,0); beat(0,0,0,16'o177546,0);
        if(got!==16'h80) $fatal(1,"KW11 high-byte write");
        beat(1,1,0,16'o177546,16'h40); repeat(410) tick;
        beat(0,0,0,16'o177546,0); if(got!==16'hc0 || ticks==0) $fatal(1,"KW11 tick/IE");
        beat(1,1,0,16'o177560,16'h40); send_uart(8'ha7);
        beat(0,0,0,16'o177560,0); if(got[7:6]!==2'b11 || !virq) $fatal(1,"KL11 receive/IRQ");
        // Speculate in ordinary FRAM while RXBUF and SD DATA have live side effects.
        old_accepted=accepted; old_edges=edges;
        beat(0,0,1,16'h0100,0); repeat(150) tick;
        if(accepted!=old_accepted || edges!=old_edges || !legacy.fixed_uart.console.rx_full)
            $fatal(1,"FRAM speculation touched a peripheral");
        beat(0,1,1,16'o177562,0); if(got!==16'ha7) $fatal(1,"RXBUF value");
        repeat(100) tick;
        if(legacy.fixed_uart.console.rx_full || virq || accepted!=old_accepted+1)
            $fatal(1,"RXBUF must clear exactly once on demand");
        beat(1,1,0,16'o177565,16'h41); beat(0,0,0,16'o177564,0);
        if(got!==16'h80) $fatal(1,"KL11 ignored high CSR write");
        beat(1,1,0,16'o177567,16'h55); repeat(30) tick;
        if(!uart_tx || legacy.fixed_uart.console.tx_busy) $fatal(1,"KL11 ignored high TXBUF write");
        beat(1,0,0,16'o177502,16'h2); if(sd_cs_n) $fatal(1,"SD control");
        old_edges=edges; old_accepted=accepted;
        beat(0,0,1,16'o177500,0); repeat(150) tick;
        if(got!==16'hff || edges-old_edges!=8 || accepted-old_accepted!=1)
            $fatal(1,"SD data read must clock exactly one byte, without prefetch");
        // Unknown I/O must remain unacknowledged, never alias FRAM.
        @(negedge clk); request=1; stream=1; address=16'o170000; old_edges=fram.transaction_count;
        repeat(100) begin tick; if(ack) $fatal(1,"unknown I/O ACK"); end
        if(fram.transaction_count!=old_edges) $fatal(1,"unknown I/O alias");
        @(negedge clk); request=0;
        $display("PASS lsi11 peripherals: %0d beats; KL11 RX/TX lanes, KW11, panel, SD side effects, unknown I/O",beats);
        $finish;
    end
    initial begin #1000000; $fatal(1,"peripheral timeout"); end
endmodule
