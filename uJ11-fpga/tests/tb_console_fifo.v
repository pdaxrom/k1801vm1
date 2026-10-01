`timescale 1ns/1ps
module tb_console_fifo;
    reg clk=0,reset=1,push=0,pop=0,configure=0,enable=0;
    always #5 clk=~clk;
    reg [7:0] data;
    wire ready,enabled;wire [31:0] value;wire [4:0] level;
    uj11_console_fifo dut(.*);
    reg [7:0] expected[0:19999];
    integer written=0,read=0,checks=0;
    task step(input bit produce,input bit consume);
        bit adding,taking;
        @(negedge clk);
        push=produce && ready;pop=consume;data=written[7:0];
        adding=push && enabled;taking=pop && level!=0;
        if(taking)begin
            if(value!={24'h000001,expected[read]})$fatal(1,"FIFO ordering %0d",read);
            read++;
        end
        if(adding)begin expected[written]=data;written++;end
        @(posedge clk);#1;
        if(level!=written-read || ready!=(level<16) || value[8]!=(level!=0))$fatal(1,"FIFO accounting");
        checks++;
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;configure=1;enable=1;
        @(negedge clk);configure=0;
        repeat(16)step(1,0);
        repeat(20)step(1,0);
        if(level!=16 || ready)$fatal(1,"full queue failed to backpressure");
        repeat(10000)step($urandom_range(0,1),$urandom_range(0,1));
        while(level)step(0,1);
        $display("PASS console FIFO: %0d checks, %0d ordered bytes",checks,read);$finish;
    end
endmodule
