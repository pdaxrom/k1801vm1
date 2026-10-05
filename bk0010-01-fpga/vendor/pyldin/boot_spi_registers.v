// HD6303 emulator's SPI register layout relocated to free addresses, using shared SD ownership:
// E660 DATA_H, E661 DATA_L/start, E662 READY/control, E663 prescaler, E664 POUT.
// control bit 1 is physical SD CS (0 selected), matching current HD emulator.
module boot_spi_registers (
    input wire clk,reset,enabled,
    input wire bus_write,
    input wire [2:0] address,
    input wire [7:0] data,
    output reg [7:0] result,
    output wire busy,
    output wire cs,
    input wire byte_busy,byte_done,
    input wire[7:0]byte_rx,
    output wire byte_start,
    output wire[7:0]byte_data,byte_divider
);
    reg[7:0]control,divider,pout,tx_high,rx_high,rx_low,byte_tx;
    reg start,high_pending,word_pending,active;
    assign busy=active;
    assign cs=control[1];
    assign byte_start=start;assign byte_data=byte_tx;assign byte_divider=divider;
    always @*begin
        case(address)
            0:result=rx_high;
            1:result=rx_low;
            2:result={enabled&&!busy,1'b0,control[5:4],2'b0,control[1:0]};
            3:result=divider;
            4:result=pout;
            default:result=8'ha5;
        endcase
    end
    reg[7:0]tx_low;
    always @(posedge clk)begin
        start<=0;
        if(reset)begin
            control<=8'h23;divider<=29;pout<=3;tx_high<=8'hff;tx_low<=8'hff;
            rx_high<=8'hff;rx_low<=8'hff;byte_tx<=8'hff;start<=0;
            high_pending<=0;word_pending<=0;active<=0;
        end else begin
            if(byte_done)begin
                if(word_pending)begin
                    rx_high<=byte_rx;byte_tx<=tx_low;start<=1;word_pending<=0;
                end else begin rx_low<=byte_rx;active<=0;end
            end
            if(enabled&&bus_write&&!busy)case(address)
                0:begin tx_high<=data;high_pending<=1;end
                1:if(control[4]&&high_pending)begin
                    byte_tx<=tx_high;tx_low<=data;word_pending<=1;start<=1;high_pending<=0;active<=1;
                end else if(!control[4])begin byte_tx<=data;start<=1;active<=1;end
                2:begin control<=data&8'h33;if(!data[4])high_pending<=0;end
                3:divider<=data;
                4:pout<=data&3;
            endcase
        end
    end
endmodule
