`timescale 1ns/1ps
module tb_plm_equivalence;
    reg [15:0] ir;
    reg [14:0] mr;
    wire [33:0] a, b, ga, gb;
    vm1a_plm original_a(ir, mr, a);
    vm1a_plm_masked derived_a(ir, mr, b);
    vm1g_plm original_g(ir, mr, ga);
    vm1g_plm_masked derived_g(ir, mr, gb);
    integer i;
    initial begin
        for (i = 0; i < 12000; i = i + 1) begin
            ir = $random; mr = $random;
            #1;
            if (a !== b || ga !== gb) $fatal(1, "PLM mismatch ir=%h mr=%h", ir, mr);
        end
        // Every microcode address for several representative opcodes.
        for (i = 0; i < 32768; i = i + 1) begin
            mr = i; ir = 16'o010001; #1;
            if (a !== b || ga !== gb) $fatal(1, "PLM state mismatch %h", mr);
        end
        $display("PASS PLM equivalence: 44768 input pairs, both VM1A and VM1G");
        $finish;
    end
endmodule
