// SPI mode 0: sample on rising SCK, change MOSI on falling SCK.
// CS belongs to the transaction owner and is never changed between bytes here.
module spi_byte_master (
    input wire clk, reset, start,
    input wire [7:0] tx_data,
    input wire [7:0] divider,
    input wire miso,
    output reg sck, mosi, busy, done,
    output reg [7:0] rx_data
);
    reg [7:0] count, tx, rx;
    reg [2:0] bit_index;
    always @(posedge clk) begin
        done <= 0;
        if (reset) begin
            sck <= 0; mosi <= 1; busy <= 0; done <= 0; count <= 0;
            tx <= 0; rx <= 0; rx_data <= 0; bit_index <= 0;
        end else if (!busy) begin
            if (start) begin
                tx <= tx_data; mosi <= tx_data[7]; rx <= 0; bit_index <= 7;
                count <= divider; busy <= 1; sck <= 0;
            end
        end else if (count != 0) count <= count - 1'b1;
        else begin
            count <= divider;
            if (!sck) begin
                sck <= 1; rx <= {rx[6:0], miso};
            end else begin
                sck <= 0;
                if (bit_index == 0) begin
                    busy <= 0; done <= 1; rx_data <= rx; mosi <= 1;
                end else begin
                    bit_index <= bit_index - 1'b1;
                    tx <= {tx[6:0],1'b1}; mosi <= tx[6];
                end
            end
        end
    end
endmodule
