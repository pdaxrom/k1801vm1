`timescale 1ns/1ps
module tb_hc7000_iop_ram;
`ifdef UJ11_IOP_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,enable=0;always #20.833 clk=~clk;
    reg [8:0] address=0;reg [3:0] we=0;reg [31:0] wd=0;
    wire [31:0] rd;reg [31:0] golden[0:511];
    reg [7:0] sector_address=0;reg sector_write=0;reg [15:0] sector_data=0;
    wire [15:0] sector_read;
    integer checks=0;
    uj11_iop_ram ram(.clk(clk),.enable(enable),.address(address),.write_enable(we),.write_data(wd),.data(rd));
    uj11_sector_ram sector(.clk(clk),.write(sector_write),.address(sector_address),.write_data(sector_data),.data(sector_read));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s at address %h: read %h expected %h",why,address,rd,golden[address]);checks++;end
    endtask
    initial begin
        $readmemh("build/hc7000-iop/firmware.mem",golden);
        repeat(5)@(negedge clk);enable=1;
        for(integer i=0;i<512;i++)begin
            address=9'(i);@(negedge clk);check(rd===golden[i],"firmware initialization");
        end
        for(integer i=0;i<512;i++)begin
            address=9'(i);we=15;wd=32'hdeadc0de^(i*32'h01030507);golden[i]=wd;
            @(negedge clk);we=0;@(negedge clk);check(rd===golden[i],"full word write");
            for(integer lane=0;lane<4;lane++)begin
                we=4'(1<<lane);wd=32'hab1289ef^(i*32'h07050301);
                golden[i][8*lane+:8]=wd[8*lane+:8];
                @(negedge clk);we=0;@(negedge clk);check(rd===golden[i],"independent byte lane");
            end
        end
        enable=0;we=15;wd=0;repeat(3)@(negedge clk);we=0;enable=1;
        @(negedge clk);check(rd===golden[address],"disabled write");
        for(integer i=0;i<256;i++)begin
            sector_address=8'(i);sector_data=16'hcafe^16'(i*257);sector_write=1;@(negedge clk);
        end
        sector_write=0;
        for(integer i=0;i<256;i++)begin
            sector_address=8'(i);@(negedge clk);
            check(sector_read===(16'hcafe^16'(i*257)),"sector address/data lanes");
        end
        $display("PASS HC7000 IOP RAM: %0d checks",checks);$finish;
    end
endmodule
