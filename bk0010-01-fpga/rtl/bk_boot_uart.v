`timescale 1ns/1ps
// Bootstrap progress only: B:01 / E:41 followed by CR/LF, 115200 8N1.
module bk_boot_uart #(parameter integer CLOCK_HZ = 24000000) (
    input wire clk, reset,
    input wire [7:0] stage,
    input wire error,
    output reg tx = 1
);
    localparam integer DIVISOR = CLOCK_HZ / 115200;
    localparam integer LIMIT = DIVISOR - 1;
    localparam integer COUNTER_BITS = DIVISOR > 1 ? $clog2(DIVISOR) : 1;
    reg [COUNTER_BITS-1:0] divider = 0;
    reg [9:0] shift = 10'h3ff;
    reg [3:0] bits = 0;
    reg [2:0] character = 0;
    reg [7:0] seen = 0, pending = 0, message_stage = 0;
    reg pending_error = 0, message_error = 0, seen_error = 0;
    reg waiting = 0, active = 0;
    function [7:0] hex;
        input [3:0] value;
        begin case(value)
            0:hex="0";1:hex="1";2:hex="2";3:hex="3";
            4:hex="4";5:hex="5";6:hex="6";7:hex="7";
            8:hex="8";9:hex="9";10:hex="A";11:hex="B";
            12:hex="C";13:hex="D";14:hex="E";default:hex="F";
        endcase end
    endfunction
    reg [7:0] value;
    always @* begin
        case(character)
            0: value = message_error ? "E" : "B";
            1: value = ":";
            2: value = hex(message_stage[7:4]);
            3: value = hex(message_stage[3:0]);
            4: value = 13;
            default: value = 10;
        endcase
    end
    always @(posedge clk) begin
        if(reset) begin
            tx <= 1; divider <= 0; shift <= 10'h3ff; bits <= 0; character <= 0;
            seen <= 0; pending <= 0; message_stage <= 0;
            pending_error <= 0; message_error <= 0; seen_error <= 0; waiting <= 0; active <= 0;
        end else begin
            if(bits != 0) begin
                if(divider != 0) divider <= divider - 1'b1;
                else begin
                    divider <= LIMIT[COUNTER_BITS-1:0]; bits <= bits - 1'b1;
                    shift <= {1'b1,shift[9:1]}; tx <= shift[1];
                end
            end else if(active) begin
                shift <= {1'b1,value,1'b0}; tx <= 0;
                divider <= LIMIT[COUNTER_BITS-1:0]; bits <= 10;
                if(character == 5) active <= 0;
                else character <= character + 1'b1;
            end else if(waiting) begin
                message_stage <= pending; message_error <= pending_error;
                waiting <= 0; character <= 0; active <= 1;
            end
            // A newly arrived event wins over consuming the old pending one.
            if(stage != seen || error != seen_error) begin
                seen <= stage; seen_error <= error;
                pending <= stage; pending_error <= error; waiting <= 1;
            end
        end
    end
endmodule
