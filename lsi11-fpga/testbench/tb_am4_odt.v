`timescale 1ns/1ps

module tb_am4_odt;
	parameter integer UART_XO2 = 1;
	parameter MICROM_FILE = "../ucode/experimental/am4/mc.rom";
	localparam integer UART_BIT_NS = 8680;
	localparam integer MAX_CYCLES = 200000;
	reg clk = 0;
	reg reset = 1;
	wire peripheral_reset;
	wire bus_request, bus_write, bus_ack;
	wire [1:0] bus_byte_select;
	wire [15:0] bus_address, bus_wdata, bus_rdata;
	wire virq, interrupt_strobe, interrupt_acknowledge, timer_event;
	wire [15:0] interrupt_vector;
	wire uart_tx;
	wire spi_cs_n, spi_sck, spi_mosi, spi_miso;
	reg [7:0] serial_byte;
	integer serial_count = 0;
	integer cycles = 0;
	reg saw_cr = 0;
	reg saw_lf = 0;

	always #18.797 clk = !clk;
	always @(posedge clk) begin
		if (!reset) cycles <= cycles + 1;
		if (cycles >= MAX_CYCLES)
			$fatal(1, "AM4 ODT timeout at micro-PC %03h address=%06o bytes=%0d",
				engine.ma, bus_address, serial_count);
	end

	task automatic uart_receive(output [7:0] data);
		integer bit_index;
		reg [7:0] sampled;
	begin
		@(negedge uart_tx);
		#(UART_BIT_NS + UART_BIT_NS / 2);
		for (bit_index = 0; bit_index < 8; bit_index = bit_index + 1) begin
			sampled[bit_index] = uart_tx;
			#UART_BIT_NS;
		end
		if (!uart_tx)
			$fatal(1, "AM4 ODT UART omitted stop bit after byte %0d", serial_count);
		data = sampled;
	end
	endtask

	am4_hc1200_cpu11_bus #(
		.UART_XO2(UART_XO2), .FRAM_CLK_DIV(1)
	) guest_bus (
		.clk(clk), .rst(reset), .peripheral_reset(peripheral_reset),
		.request(bus_request), .write(bus_write),
		.byte_select(bus_byte_select), .address(bus_address),
		.wdata(bus_wdata), .rdata(bus_rdata), .acknowledge(bus_ack),
		.virq(virq), .interrupt_vector(interrupt_vector),
		.interrupt_strobe(interrupt_strobe),
		.interrupt_acknowledge(interrupt_acknowledge),
		.event_irq(timer_event), .timer_enable(1'b0),
		.uart_rx(1'b1), .uart_tx(uart_tx), .spi_cs_n(spi_cs_n),
		.spi_sck(spi_sck), .spi_mosi(spi_mosi), .spi_miso(spi_miso),
		.sd_cs_n(), .sd_sck(), .sd_mosi(), .sd_miso(1'b1),
		.boot_rom_ena(), .boot_rom_addr(), .boot_rom_data(8'b0),
		.boot_complete()
	);

	am4_direct #(
		.MICROM_FILE(MICROM_FILE), .BUS_TIMEOUT_BITS(9)
	) engine (
		.clk(clk), .peripheral_reset(peripheral_reset), .reset(reset),
		.power_fail(1'b0), .halt_request(1'b0),
		.event_request(timer_event), .vector_irq(virq),
		.bus_address(bus_address), .bus_write_data(bus_wdata),
		.bus_read_data(bus_rdata), .bus_request(bus_request),
		.bus_write(bus_write), .bus_byte_select(bus_byte_select),
		.bus_ready(bus_ack), .vector_data(interrupt_vector),
		.vector_ready(interrupt_acknowledge),
		.vector_request(interrupt_strobe), .boot_rom_ena(1'b0),
		.boot_rom_addr(10'b0), .boot_rom_data(), .boot_select(2'b11)
	);

	spi_fram_model fram (
		.cs_n(spi_cs_n), .sck(spi_sck), .mosi(spi_mosi), .miso(spi_miso)
	);

	initial begin
		forever begin
			uart_receive(serial_byte);
			serial_count = serial_count + 1;
			$write("%c", serial_byte);
			if (serial_byte == 8'd13) saw_cr = 1;
			if (serial_byte == 8'd10) saw_lf = 1;
			if (serial_byte == "@") begin
				if (!saw_cr || !saw_lf || serial_count < 5)
					$fatal(1, "AM4 ODT prompt was incomplete");
				$display("\nPASS: AM4 reset vector executed HALT and ODT reached physical KL11 TX (%0d clocks, %0d bytes)",
					cycles, serial_count);
				$finish;
			end
		end
	end

	initial begin
		repeat (15) @(negedge clk);
		reset = 0;
	end

endmodule
