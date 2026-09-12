`timescale 1ns/1ps
module tb_button_cp63 #(parameter integer HOLD=100);
    reg clk=0,power_on=1,button_n=1;
    wire hard_reset,halt_pulse;
    integer pulses=0,checks=0,phase;
    uj11_button #(.SAMPLE_DIVISOR(7),.HOLD_SAMPLES(HOLD)) dut(.*);
    always #5 clk=~clk;
    always @(posedge clk) if(halt_pulse) pulses<=pulses+1;
    task samples(input integer n);
        repeat(n) begin @(negedge clk);wait(dut.sample);@(negedge clk);end
    endtask
    task eq(input integer got,want,input string why);
        begin if(got!==want)$fatal(1,"%s got %0d want %0d",why,got,want);checks++;end
    endtask
    initial begin
        repeat(4)@(negedge clk);power_on=0;
        samples(4);eq(hard_reset,0,"released boot");eq(pulses,0,"boot has no short event");
        // Sub-sample bounce never becomes a confirmed press.
        button_n=0;repeat(2)@(negedge clk);button_n=1;
        samples(4);eq(pulses,0,"bounce ignored");
        button_n=0;samples(5);eq(hard_reset,0,"short press preserves system");
        button_n=1;samples(4);eq(pulses,1,"one release pulse");
        samples(5);eq(pulses,1,"held release no repeat");
        button_n=0;samples(HOLD+4);eq(hard_reset,1,"long press resets before release");
        samples(30);eq(hard_reset,1,"reset stays asserted while held");
        button_n=1;samples(4);eq(hard_reset,0,"long release restarts");eq(pulses,1,"long release not short");
        // Long reset is unrelated to guest execution or any guest RESET pulse.
        power_on=1;button_n=0;repeat(4)@(negedge clk);power_on=0;
        samples(20);eq(hard_reset,1,"held power on");
        button_n=1;samples(4);eq(hard_reset,0,"held boot release");eq(pulses,1,"held boot no ODT");
        button_n=0;samples(4);button_n=1;samples(4);eq(pulses,2,"rearmed after reset");
        for(phase=0;phase<7;phase=phase+1)begin
            repeat(phase)@(negedge clk);
            button_n=0;samples(4);eq(hard_reset,0,"short press at each clock phase");
            button_n=1;samples(4);eq(pulses,3+phase,"one event at each clock phase");
        end
        $display("PASS CP63 button: %0d checks HOLD=%0d",checks,HOLD);$finish;
    end
    initial begin #100000;$fatal(1,"button timeout");end
endmodule
