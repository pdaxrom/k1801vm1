`timescale 1ns/1ps
// Compare full-length production periods with an independent cycle count,
// including reset part-way through a period. Do not duplicate the LFSR.
module tick_check #(parameter DIVISOR=591200)(output reg done=0);
    reg clk=0, reset=1;
    wire terminal;
    integer cycle, period, epoch;
    always #5 clk=~clk;
    uj11_tick #(.DIVISOR(DIVISOR)) dut(clk,reset,terminal);
    initial begin
        for(epoch=0;epoch<2;epoch=epoch+1)begin
            @(negedge clk); reset=1;
            repeat(2) @(negedge clk);
            reset=0;
            for(period=0;period<3;period=period+1)
                for(cycle=1;cycle<=DIVISOR;cycle=cycle+1)begin
                    @(posedge clk);
                    if(terminal !== (cycle==DIVISOR))
                        $fatal(1,"divisor %0d epoch %0d period %0d cycle %0d",DIVISOR,epoch,period,cycle);
                end
            repeat(DIVISOR/3) @(negedge clk);
        end
        done=1;
    end
endmodule

module tb_tick;
    wire nominal,calibrated,minimum,maximum;
    tick_check #(.DIVISOR(591200)) a(nominal);
    tick_check #(.DIVISOR(596037)) b(calibrated);
    tick_check #(.DIVISOR(1)) c(minimum);
    tick_check #(.DIVISOR(1048575)) d(maximum);
    initial begin
        wait(nominal && calibrated && minimum && maximum);
        $display("PASS KW11: nominal, calibrated and boundary divisors; exact periods and mid-period reset");
        $finish;
    end
    initial begin #100000000; $fatal(1,"KW11 timeout"); end
endmodule
