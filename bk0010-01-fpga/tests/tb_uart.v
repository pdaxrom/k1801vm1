`timescale 1ns/1ps
module tb_uart;
    reg clk = 0, reset = 1, error = 0;
    reg [7:0] stage = 0;
    always #5 clk = !clk;
    wire tx;
    reg [7:0] received[0:31], value;
    integer count = 0, n;
    bk_boot_uart #(.CLOCK_HZ(1152000)) uart(clk,reset,stage,error,tx);
    // Decode the serial pin, 10 source cycles per bit, rather than internal state.
    initial forever begin
        @(negedge tx); #150;
        for(n=0;n<8;n=n+1) begin value[n] = tx; #100; end
        if(!tx) $fatal(1,"UART stop bit");
        received[count] = value; count = count + 1;
    end
    task check(input integer offset, input [7:0] kind, high_hex, low_hex);
        begin
            if(received[offset] != kind || received[offset+1] != ":" || received[offset+2] != high_hex ||
               received[offset+3] != low_hex || received[offset+4] != 13 || received[offset+5] != 10)
                $fatal(1,"UART packet %0d",offset);
        end
    endtask
    initial begin
        #100; reset = 0; stage = 1;
        wait(count == 1); stage = 2; #100; stage = 3; #100; stage = 4;
        wait(count == 12); check(0,"B","0","1"); check(6,"B","0","4");
        error = 1; // Error without a stage change must still be reported.
        wait(count == 18); check(12,"E","0","4");
        $display("PASS UART pin decoding: 8N1, CR/LF, coalesced stages and same-stage error");
        $finish;
    end
    initial begin #100000; $fatal(1,"UART timeout"); end
endmodule
