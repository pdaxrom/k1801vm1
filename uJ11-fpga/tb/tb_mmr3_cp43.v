`timescale 1ns/1ps
module tb_mmr3_cp43;
    reg clk=0,reset=1,request=0,writing=0,low_byte_enable=0;
    reg [5:0] write_data=0;
    wire [5:0] value;wire ready;
    reg [5:0] expected=0;
    integer datum,mask,check_count=0;
    always #5 clk=~clk;
    uj11_mmr3 dut(.*);
    task edge_check(input bit ack_expected);
        begin
            @(posedge clk);#1;
            if(ready!==ack_expected || value!==expected)
                $fatal(1,"MMR3 ack/value expected %b/%o got %b/%o",ack_expected,expected,ready,value);
            check_count=check_count+1;
            @(negedge clk);
        end
    endtask
    initial begin
        edge_check(0);reset=0;
        for(datum=0;datum<64;datum=datum+1)begin
            for(mask=0;mask<4;mask=mask+1)begin
                request=1;writing=mask[1];low_byte_enable=mask[0];write_data=datum[5:0];
                if(writing && low_byte_enable)expected=write_data;
                edge_check(1);
                // A held request obeys the stable-control/data bus contract.
                edge_check(1);edge_check(1);request=0;edge_check(0);
            end
        end
        // Reset wins over an active write; request held after reset is new.
        request=1;writing=1;low_byte_enable=1;write_data=6'o77;reset=1;expected=0;
        edge_check(0);edge_check(0);reset=0;expected=6'o77;edge_check(1);
        request=0;edge_check(0);reset=1;expected=0;edge_check(0);
        $display("PASS MMR3 unit: %0d checked edges; all values/read-write/lane/hold/reset",check_count);
        $finish;
    end
    initial begin #20000;$fatal(1,"watchdog");end
endmodule
