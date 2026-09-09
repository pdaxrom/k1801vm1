`timescale 1ns/1ps
module tb_control_store;
    reg clk=0, reset=1, enable=0;
    wire [35:0] uword;
    wire [9:0] upc, next_address;
    reg [35:0] expected[0:1023];
    reg [9:0] trace[0:16];
    integer i,j;
    uj11_microseq seq(.trace_pending(1'b0),.irq_pending(1'b0),.fault_redirect(1'b0),.fault_repair(1'b0),.clk(clk),.reset(reset),.enable(enable),.uword(uword),
        .ir(16'h0a18),.nzvc(4'h1),.dispatch_address(10'h110),
        .address_odd(1'b1),.byte_instruction(1'b1),.selected_a(4'd4),
        .q0(1'b0),.loop_zero(1'b0),.bus_error(1'b0),.a_one(1'b0),
        .upc(upc),.next_address(next_address));
    uj11_rom rom(.clk(clk),.enable(reset|enable),.address(next_address),.data(uword));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    initial begin
        $readmemh("microcode/generated/checkpoint_seq.mem",expected);
        trace[0]='h010; trace[1]='h012; trace[2]='h030; trace[3]='h013;
        trace[4]='h045; trace[5]='h020; trace[6]='h04b; trace[7]='h021;
        trace[8]='h050; trace[9]='h022; trace[10]='h057; trace[11]='h023;
        trace[12]='h059; trace[13]='h024; trace[14]='h110; trace[15]='h112; trace[16]='h020;
        #100;
        @(posedge clk); #1;
        if(upc!==0 || uword!==expected[0]) $fatal(1,"reset ROM priming");
        @(negedge clk); reset=0;
        for(i=0;i<17;i=i+1) begin
            // Stall before every instruction, including CALL and RETURN.
            for(j=0;j<(i%3)+1;j=j+1) begin
                @(posedge clk); #1;
                if(upc !== ((i==0) ? 10'h0 : trace[i-1]) || uword!==expected[upc])
                    $fatal(1,"stalled uIR/upc alignment");
            end
            @(negedge clk); enable=1;
            @(posedge clk); #1; enable=0;
            if(upc!==trace[i] || uword!==expected[trace[i]])
                $fatal(1,"ROM feedback i=%d upc=%h expected=%h word=%h",i,upc,trace[i],uword);
        end
        $display("PASS control store: 17 transitions, no sequencing bubbles, stalls preserve uIR alignment");
        $finish;
    end
    initial begin #20000; $fatal(1,"control-store timeout"); end
endmodule
