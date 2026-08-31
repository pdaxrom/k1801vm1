// Fixed HC1200 specialization of cpu11 xen/lib/wbc_uart.v.
// Copyright (c) 2014-2019 by 1801BM1@gmail.com
// Original source: cpu11 commit e0576637a35f0378f85673213a808098b9535526
//
// The board contract is fixed at 115200/8/N/1. Register addresses, CSR ready
// and IE bits, RX/TX interrupt handshakes, active-low CTS and RTS/DTR behavior
// match wbc_uart. Variable word length, parity and test mode are omitted.
module wbc_uart_xo2 #(parameter REFCLK=26600000)
(
	input wire wb_clk_i,
	input wire wb_rst_i,
	input wire [2:0] wb_adr_i,
	input wire [15:0] wb_dat_i,
	output wire [15:0] wb_dat_o,
	input wire wb_cyc_i,
	input wire wb_we_i,
	input wire wb_stb_i,
	output reg wb_ack_o,
	output wire tx_dat_o,
	input wire tx_cts_i,
	input wire rx_dat_i,
	output wire rx_dtr_o,
	output reg tx_irq_o,
	input wire tx_ack_i,
	output reg rx_irq_o,
	input wire rx_ack_i
);
	localparam integer BIT_TICKS = (REFCLK + 57600) / 115200;
	localparam integer HALF_TICKS = BIT_TICKS / 2;
	localparam integer TIMER_WIDTH = $clog2(BIT_TICKS);

	reg [1:0] cts_sync;
	reg [1:0] rx_sync;
	reg tx_ie;
	reg tx_ready;
	reg tx_break;
	reg [7:0] tx_hold;
	reg [8:0] tx_shift;
	reg [TIMER_WIDTH-1:0] tx_timer;
	reg [3:0] tx_bits;
	reg tx_busy;
	reg rx_ie;
	reg rx_full;
	reg rx_overflow;
	reg rx_break;
	reg [7:0] rx_buffer;
	reg [7:0] rx_shift;
	reg [TIMER_WIDTH-1:0] rx_timer;
	reg [3:0] rx_bits;
	reg rx_busy;
	reg tx_irq_condition;
	reg rx_irq_condition;

	wire request = wb_cyc_i && wb_stb_i;
	wire accept = request && !wb_ack_o;
	wire rx_csr_write = accept && wb_we_i && wb_adr_i[2:1] == 2'b00;
	wire rx_buffer_read = accept && !wb_we_i && wb_adr_i[2:1] == 2'b01;
	wire tx_csr_write = accept && wb_we_i && wb_adr_i[2:1] == 2'b10;
	wire tx_buffer_write = accept && wb_we_i && wb_adr_i[2:1] == 2'b11;
	wire rx_data = rx_sync[1];
	wire tx_condition = tx_ready && tx_ie;
	wire rx_condition = rx_full && rx_ie;

	assign wb_dat_o = wb_adr_i[2:1] == 2'b00 ?
		{rx_break, 2'b0, rx_overflow, 4'b0, rx_full, rx_ie, 6'b0} :
		wb_adr_i[2:1] == 2'b01 ? {8'b0, rx_buffer} :
		wb_adr_i[2:1] == 2'b10 ?
		{8'b0, tx_ready, tx_ie, 5'b0, tx_break} : 16'b0;
	assign tx_dat_o = (tx_busy ? tx_shift[0] : 1'b1) && !tx_break;
	assign rx_dtr_o = rx_full;

	always @(posedge wb_clk_i or posedge wb_rst_i) begin
		if (wb_rst_i)
			wb_ack_o <= 0;
		else
			wb_ack_o <= request && !wb_ack_o;
	end

	always @(posedge wb_clk_i or posedge wb_rst_i) begin
		if (wb_rst_i) begin
			cts_sync <= 0;
			rx_sync <= 2'b11;
		end else begin
			cts_sync <= {cts_sync[0], !tx_cts_i};
			rx_sync <= {rx_sync[0], rx_dat_i};
		end
	end

	// One cpu11-compatible holding register feeds a fixed 8/N/1 shifter.
	always @(posedge wb_clk_i or posedge wb_rst_i) begin
		if (wb_rst_i) begin
			tx_ie <= 0;
			tx_ready <= 1;
			tx_break <= 0;
			tx_hold <= 0;
			tx_shift <= 9'h1ff;
			tx_timer <= 0;
			tx_bits <= 0;
			tx_busy <= 0;
		end else begin
			if (tx_csr_write) begin
				tx_ie <= wb_dat_i[6];
				tx_break <= wb_dat_i[0];
			end
			if (tx_buffer_write) begin
				tx_ready <= 0;
				tx_hold <= wb_dat_i[7:0];
			end

			if (tx_busy) begin
				if (tx_timer != 0) begin
					tx_timer <= tx_timer - 1'b1;
				end else if (tx_bits == 1) begin
					tx_busy <= 0;
					tx_bits <= 0;
				end else begin
					tx_timer <= BIT_TICKS - 1;
					tx_bits <= tx_bits - 1'b1;
					tx_shift <= {1'b1, tx_shift[8:1]};
				end
			end else if (!tx_ready && cts_sync[1]) begin
				tx_busy <= 1;
				tx_ready <= !tx_buffer_write;
				tx_timer <= BIT_TICKS - 1;
				// Start + eight data bits + a complete stop bit.  Keeping
				// the stop interval in the busy state is essential when the
				// holding register already contains the following character.
				tx_bits <= 10;
				tx_shift <= {tx_hold, 1'b0};
			end
		end
	end

	// Start is checked at half a bit; data and stop are sampled at bit centers.
	always @(posedge wb_clk_i or posedge wb_rst_i) begin
		if (wb_rst_i) begin
			rx_ie <= 0;
			rx_full <= 0;
			rx_overflow <= 0;
			rx_break <= 0;
			rx_buffer <= 0;
			rx_shift <= 0;
			rx_timer <= 0;
			rx_bits <= 0;
			rx_busy <= 0;
		end else begin
			if (rx_csr_write)
				rx_ie <= wb_dat_i[6];
			if (rx_buffer_read) begin
				rx_full <= 0;
				rx_overflow <= 0;
			end

			if (!rx_busy) begin
				if (!rx_data) begin
					rx_busy <= 1;
					rx_bits <= 0;
					rx_timer <= HALF_TICKS - 1;
				end
			end else if (rx_timer != 0) begin
				rx_timer <= rx_timer - 1'b1;
			end else if (rx_bits == 0) begin
				if (rx_data) begin
					rx_busy <= 0;
				end else begin
					rx_bits <= 1;
					rx_timer <= BIT_TICKS - 1;
				end
			end else if (rx_bits <= 8) begin
				rx_shift <= {rx_data, rx_shift[7:1]};
				rx_bits <= rx_bits + 1'b1;
				rx_timer <= BIT_TICKS - 1;
			end else begin
				rx_full <= 1;
				rx_buffer <= rx_shift;
				rx_overflow <= rx_full;
				rx_break <= !rx_data;
				rx_busy <= 0;
				rx_bits <= 0;
			end
		end
	end

	// Acknowledge clears only the request latch; another request needs a new
	// false-to-true ready condition, matching the original cpu11 peripheral.
	always @(posedge wb_clk_i or posedge wb_rst_i) begin
		if (wb_rst_i) begin
			tx_irq_condition <= 0;
			rx_irq_condition <= 0;
			tx_irq_o <= 0;
			rx_irq_o <= 0;
		end else begin
			tx_irq_condition <= tx_condition;
			rx_irq_condition <= rx_condition;
			if (tx_ack_i || !tx_condition)
				tx_irq_o <= 0;
			else if (tx_condition && !tx_irq_condition)
				tx_irq_o <= 1;
			if (rx_ack_i || rx_buffer_read || !rx_condition)
				rx_irq_o <= 0;
			else if (rx_condition && !rx_irq_condition)
				rx_irq_o <= 1;
		end
	end
endmodule
