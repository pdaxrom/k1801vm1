`timescale 1ns/1ps
module tb_terak_button;
    reg clk=0,power_on=1,button_n=1,idle=1;
    always #5 clk=~clk;
    wire guest,cold;
    integer pulses=0;
    uj11_terak_button #(.SAMPLE_DIVISOR(4),.MEDIUM_SAMPLES(10),.LONG_SAMPLES(25)) dut(
        .clk(clk),.power_on(power_on),.button_n(button_n),.storage_idle(idle),
        .guest_reset(guest),.cold_reset(cold));
    always @(posedge clk)if(cold)pulses++;
    initial begin
        repeat(5)@(negedge clk);power_on=0;
        button_n=0;repeat(2)@(negedge clk);button_n=1;
        repeat(20)@(negedge clk);
        if(guest || cold)$fatal(1,"button bounce caused reset");
        button_n=0;wait(dut.duration==3);@(negedge clk);button_n=1;
        wait(dut.duration==0);repeat(10)@(negedge clk);
        if(guest || cold || pulses)$fatal(1,"short press caused reset");
        button_n=0;wait(dut.duration==12);@(negedge clk);
        if(!guest || cold)$fatal(1,"medium press did not reset only the guest");
        button_n=1;wait(dut.duration==0);repeat(10)@(negedge clk);
        if(guest || cold || pulses)$fatal(1,"medium release restarted SERV");
        idle=0;button_n=0;wait(dut.duration==25);repeat(12)@(negedge clk);
        if(!guest || cold)$fatal(1,"long reset interrupted busy storage");
        button_n=1;wait(dut.duration==0);repeat(10)@(negedge clk);
        if(!guest || cold)$fatal(1,"pending long reset was lost on release");
        idle=1;wait(cold);repeat(8)@(negedge clk);
        if(guest || cold || pulses!=3)$fatal(1,"cold reset did not complete exactly once");
        $display("PASS button debounce, ignored short press, warm reset, pending long reset and storage drain");
        $finish;
    end
    initial begin #100000;$fatal(1,"button timeout");end
endmodule
