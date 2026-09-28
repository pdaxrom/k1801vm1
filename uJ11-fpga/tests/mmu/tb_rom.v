`timescale 1ns/1ps
module tb_rom;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,enable=0;always #5 clk=~clk;
    reg [11:0] address=0;
    wire [53:0] data;
    wire fpp_enabled,pipeline_enabled;
    reg [53:0] expected[0:3071];
    uj11_mmu_rom dut(.*);
    initial begin
        $readmemh("build/hc7000-mmu-hardware/microcode.mem",expected);
        repeat(20)@(negedge clk);
        // Cycle through banks at each location, including every high target
        // bit and lane boundary. Check that clock-enable holds bank and data.
        for(integer i=0;i<1024;i++)for(integer bank=0;bank<3;bank++)begin
            address=12'(bank*1024+i);enable=1;
            @(negedge clk);
            if(data!==expected[bank*1024+i])$fatal(1,"ROM %h got %h want %h",address,data,expected[bank*1024+i]);
            enable=0;address=address^12'h800;
            @(negedge clk);
            if(data!==expected[bank*1024+i])$fatal(1,"ROM CE hold failed");
        end
        $display("PASS MMU ROM: 3072 words, 6144 checks");$finish;
    end
endmodule
