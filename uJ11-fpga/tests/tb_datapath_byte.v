`timescale 1ns/1ps
module tb_datapath_byte;
    reg clk=0,reset=0,enable=0,carry=0,byte_mode=0;
    reg [3:0] a=0,b=0,operation=0;
    reg [2:0] pair=6,destination=1;
    reg [15:0] d=0;
    wire [15:0] read_a,read_b,result,q,writeback;
    wire [3:0] nzvc;wire rf_write;
    reg [15:0] old_value,want;
    integer regno,value,kind,width,checks=0;
    uj11_datapath dut(.*);
    always #5 clk=~clk;
    task commit;begin @(negedge clk);enable=1;@(posedge clk);#1;enable=0;end endtask
    initial begin
        reset=1;@(posedge clk);#1;reset=0;
        for(regno=0;regno<16;regno=regno+1)for(value=0;value<256;value=value+1)
        for(kind=5;kind<=6;kind=kind+1)for(width=0;width<2;width=width+1)begin
            b=regno;destination=1;byte_mode=0;
            old_value={(value[7:0]^8'ha5),8'h3c};d=old_value;commit;
            destination=kind;byte_mode=width;d={~value[7:0],value[7:0]};
            want=width?(kind==5?{old_value[15:8],value[7:0]}:{{8{value[7]}},value[7:0]}):d;
            #1;if(writeback!==want)$fatal(1,"byte debug/PC writeback %h/%h",writeback,want);
            commit;if(read_b!==want)$fatal(1,"byte register merge reg%0d kind%0d width%0d",regno,kind,width);
            if(q!==0)$fatal(1,"byte RF write changed Q");checks=checks+1;
        end
        $display("PASS byte datapath: %0d register/temporary/PC merge/sign-extension/word-fallback checks",checks);$finish;
    end
endmodule
