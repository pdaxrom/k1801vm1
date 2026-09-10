`timescale 1ns/1ps
module tb_mmr3_oracle_cp43;
    reg clk=0,reset=1,request=0;
    reg [1:0] lanes=0;
    reg [15:0] datum=0,expected=0;
    wire [5:0] value;wire ready;
    integer fd,fields,checks=0,resets=0;
    always #5 clk=~clk;
    uj11_mmr3 dut(.clk(clk),.reset(reset),.request(request),.writing(1'b1),
        .low_byte_enable(lanes[0]),.write_data(datum[5:0]),.value(value),.ready(ready));
    initial begin
        repeat(3)@(negedge clk);
        fd=$fopen("build/cp43-mmr3-oracle.txt","r");if(!fd)$fatal(1,"missing oracle");
        while(!$feof(fd))begin
            fields=$fscanf(fd,"%h %h %h %h\n",reset,lanes,datum,expected);
            if(fields!=4)$fatal(1,"malformed oracle");
            request=1;
            @(posedge clk);#1;
            if({10'b0,value}!==expected || ready!==!reset)
                $fatal(1,"oracle case%0d: reset%b lanes%b data%h expected%h got%o",checks,reset,lanes,datum,expected,value);
            @(negedge clk);request=0;
            @(negedge clk);checks=checks+1;if(reset)resets=resets+1;
        end
        $fclose(fd);
        if(checks!=262160 || resets!=16)$fatal(1,"incomplete oracle");
        $display("PASS MMR3 C oracle: %0d commands, %0d RESET; all word values and four lane masks",checks,resets);
        $finish;
    end
    initial begin #6000000;$fatal(1,"watchdog");end
endmodule
