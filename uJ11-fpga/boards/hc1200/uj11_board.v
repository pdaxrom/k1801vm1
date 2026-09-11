// UJ11_MMU is opt-in: undefined = CP40h, defined = experimental CP47c.
`timescale 1ns/1ps
// CP28 complete board system. A single legacy FRAM transport owns the pins.
`ifdef UJ11_MMU
// CP44 direct relocation cost gate; original CPU, no PDR/abort/restart.
`else
// No prefetch in this initial full-peripheral resource baseline; no MMU.
`endif
module uj11_board #(
    parameter integer CLOCK_HZ=29560000, TICK_DIVISOR=591200,
    parameter integer SD_SLOW_DIV=68, SD_FAST_DIV=2
) (
    input wire clk, reset, uart_rx,
    output wire uart_tx,
    input wire [3:0] panel_keys,
    output wire panel_din, panel_ce, panel_clk, panel_rs, panel_blank, panel_latch,
    output wire host_miso, host_miso_oe,
    output wire fram_cs_n, fram_sck, fram_mosi,
    input wire fram_miso,
    output wire sd_cs_n, sd_sck, sd_mosi,
    input wire sd_miso,
    output wire boot_complete, stopped
);
    wire request, writing, byte_access, acknowledge, error, raw_ack;
`ifdef UJ11_MMU
    wire apr_request, apr_grant, apr_csr_request, apr_csr_ack;
    wire [6:0] apr_address;
    wire [5:0] apr_entry;
    wire apr_pdr;
    wire [15:0] apr_data;
    wire [21:0] address;
    wire [15:0] virtual_address, data, rdata, lane_data, lane_rdata;
    wire mmu_enabled, map22, mmu_bypass;
`else
    wire [15:0] address, data, rdata, lane_data, lane_rdata;
`endif
    wire [1:0] lanes=byte_access ? (address[0] ? 2'b10 : 2'b01) : 2'b11;
    assign lane_data=byte_access && address[0] ? {data[7:0],8'b0} : data;
    assign rdata=lane_rdata; // operand byte lanes are aligned inside the CPU
    wire [35:0] uword;
    wire opcode_fetch=uword[35] && uword[34:31]==4'd2;
    wire peripheral_reset, irq_ack, device_irq, event_irq;
    wire [15:0] device_vector;
    wire [2:0] device_priority;
    reg timer_pending;
    wire select_timer=timer_pending && !(device_irq && device_priority==7);
    wire irq_valid=timer_pending || device_irq;
    wire [2:0] irq_priority=select_timer ? 3'd6 : device_priority;
    wire [15:0] irq_vector=select_timer ? 16'o100 : device_vector;
    wire device_ack=irq_ack && !select_timer;
    always @(posedge clk) begin
        if(reset || peripheral_reset) timer_pending<=0;
        else timer_pending<=event_irq || (timer_pending && !(irq_ack && select_timer));
    end
    // AM4 peripherals need one sampled request-low edge between accepted beats.
    reg turnaround;
`ifdef UJ11_MMU
    wire translated_request;
    wire bus_request=translated_request && !turnaround;
`else
    wire bus_request=request && !turnaround;
`endif
    reg [10:0] timeout_count;
    wire timeout=&timeout_count;
    assign acknowledge=bus_request && (raw_ack || timeout);
    assign error=timeout && !raw_ack;
    always @(posedge clk) begin
        if(reset) begin turnaround<=0; timeout_count<=0; end
        else begin
            turnaround<=request && acknowledge;
            if(!bus_request || raw_ack) timeout_count<=0;
            else if(!timeout) timeout_count<=timeout_count+1'b1;
        end
    end
    uj11_core #(.ROM_DECODE(1),.ALIGNED_WORD_READS(1),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) cpu(
        .clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(irq_priority),.irq_vector(irq_vector[15:1]),
        .irq_ack(irq_ack),.waiting(),.peripheral_reset(peripheral_reset),
`ifdef UJ11_MMU
        .mem_addr(virtual_address),.mem_write_data(data),.mem_request(request),.mem_read(),
`else
        .mem_addr(address),.mem_write_data(data),.mem_request(request),.mem_read(),
`endif
        .mem_write(writing),.mem_byte(byte_access),.mem_ack(acknowledge),.mem_error(error),.mem_read_data(rdata),
        .stopped(stopped),.fault_code(),.retire(),.debug_upc(),.debug_uword(uword),
        .ir(),.mdr(),.psw(),.q(),.debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_MMU
    uj11_mmu_relocate relocate(.clk(clk),.reset(reset),.enabled(mmu_enabled && !mmu_bypass),
        .map22(map22),.request(request),.acknowledge(acknowledge),.virtual_address(virtual_address),
        .lookup_request(apr_request),.lookup_address(apr_address),.lookup_grant(apr_grant),.lookup_data(apr_data),
        .ram_region(),.io_region(),.translated_request(translated_request),.physical_address(address));
    uj11_mmu_apr_shared apr(.clk(clk),.reset(reset),.request(apr_csr_request),
        .entry(apr_entry),.pdr_select(apr_pdr),.writing(writing),.mark_written(1'b0),
        .byte_enable(lanes),.write_data(lane_data),.read_data(apr_data),.ready(apr_csr_ack),.busy(),
        .lookup_request(apr_request),.lookup_address(apr_address),.lookup_grant(apr_grant));
`else
`endif
    wire rom_enable;
    wire [8:0] rom_address;
    wire [15:0] rom_data;
    wire [1:0] rom_write;
    uj11_firmware_rom firmware(.clk(clk),.enable(rom_enable),.address(rom_address),.data(rom_data),
        .write_enable(rom_write),.write_data(lane_data));
    uj11_board_bus #(.CLOCK_HZ(CLOCK_HZ),.TICK_DIVISOR(TICK_DIVISOR),.FRAM_CLK_DIV(1),
        .SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(1),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV)) bus(
        .clk(clk),.rst(reset),.peripheral_reset(peripheral_reset),.request(bus_request),.write(writing),
`ifdef UJ11_MMU
        .mmu_enabled(mmu_enabled),.map22(map22),.mmu_bypass(mmu_bypass),
        .virtual_address(virtual_address),.memory_writing(uword[35] && uword[34:31]==4'd12),
`else
`endif
        .byte_select(lanes),.address(address),.wdata(lane_data),.instruction_fetch(opcode_fetch),
`ifdef UJ11_MMU
        .apr_request(apr_csr_request),.apr_entry(apr_entry),.apr_pdr(apr_pdr),
        .apr_data(apr_data),.apr_ack(apr_csr_ack),.rdata(lane_rdata),.acknowledge(raw_ack),.virq(device_irq),.interrupt_vector(device_vector),
`else
        .rdata(lane_rdata),.acknowledge(raw_ack),.virq(device_irq),.interrupt_vector(device_vector),
`endif
        .interrupt_priority(device_priority),.interrupt_strobe(device_ack),.interrupt_acknowledge(),.event_irq(event_irq),
        .uart_rx(uart_rx),.uart_tx(uart_tx),.panel_key_rows(panel_keys),
        .panel_din(panel_din),.panel_ce(panel_ce),.panel_clk(panel_clk),.panel_rs(panel_rs),
        .panel_blank(panel_blank),.panel_reg_latch(panel_latch),.host_miso(host_miso),.host_miso_oe(host_miso_oe),
        .spi_cs_n(fram_cs_n),.spi_sck(fram_sck),.spi_mosi(fram_mosi),.spi_miso(fram_miso),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .boot_rom_ena(rom_enable),.boot_rom_addr(rom_address),.boot_rom_write(rom_write),.boot_rom_data(rom_data),.boot_complete(boot_complete));
endmodule
