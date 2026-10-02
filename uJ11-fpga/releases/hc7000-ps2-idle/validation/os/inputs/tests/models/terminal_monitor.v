`timescale 1ns/1ps
// Independent byte-order oracle at the console fork and SERV queue read.
module terminal_monitor(input wire clk,reset,push,pop,
    input wire [7:0] written,read,output wire empty);
    reg [7:0] bytes[0:65535];
    integer puts=0,gets=0;
    assign empty=puts==gets;
    always @(posedge clk)begin
        if(reset)begin puts=0;gets=0;end
        else begin
            if(push)begin
                if(puts>=65536)$fatal(1,"terminal monitor capacity");
                bytes[puts]=written;puts++;
            end
            if(pop)begin
                if(gets>=puts || read!=bytes[gets])$fatal(1,"SERV console byte %0d mismatch",gets);
                gets++;
            end
        end
    end
    final begin
        if(!empty)$fatal(1,"terminal has %0d undelivered bytes",puts-gets);
        $display("PAL mirror consumed %0d console bytes in order",gets);
    end
endmodule
