`timescale 1ns/1ps
module tb_memory;
    reg clk = 0, power_on = 1, reset = 1, cpu_request = 0, cpu_write = 0;
    always #20.833333 clk = !clk;
    reg [19:0] cpu_address = 0;
    reg [15:0] cpu_data = 0;
    reg [1:0] cpu_lanes = 3;
    reg video_request = 0, enable_video = 0;
    reg [19:0] video_address = 8192;
    wire cpu_ready, video_ready, request, writing, ready, initialized;
    wire [19:0] address, pins_address;
    wire [15:0] data, result, pins_data;
    wire [1:0] lanes;
    wire ce, oe, we, lb, ub;
    integer i, video_words = 0;
    bk_sram_arbiter arbiter(clk,reset,cpu_request,cpu_write,cpu_address,cpu_data,cpu_lanes,cpu_ready,
                           video_request,video_address,video_ready,request,writing,address,data,lanes,ready);
    uj11_sram #(.CLEAR_WORDS(16)) controller(clk,power_on,reset && initialized,request,writing,
                                            address,lanes,data,result,ready,initialized,
                                            pins_address,pins_data,ce,oe,we,lb,ub);
    sram_model ram(pins_address,pins_data,ce,oe,we,lb,ub);
    always @(posedge clk) begin
        if(reset) video_request <= 0;
        else if(video_ready) begin
            if(result !== 16'ha5a5) $fatal(1,"Video owner received another transfer's data");
            video_request <= 0; video_address <= video_address + 1'b1; video_words <= video_words + 1;
        end else if(enable_video && !video_request) video_request <= 1;
    end
    task exchange(input wr, input [19:0] addr, input [1:0] mask, input [15:0] value, expected);
        begin
            @(negedge clk); cpu_request = 1; cpu_write = wr; cpu_address = addr; cpu_lanes = mask; cpu_data = value;
            wait(cpu_ready); #1;
            if(!wr && result !== expected) $fatal(1,"Memory %h got %h expected %h",addr,result,expected);
            @(negedge clk); cpu_request = 0;
            wait(!ready && arbiter.owner == 0);
        end
    endtask
    initial begin
        for(i=0;i<32768;i=i+1) ram.memory[i] = 16'ha5a5;
        #200; power_on = 0;
        wait(initialized); @(negedge clk); reset = 0;
        for(i=0;i<16;i=i+1) exchange(0,i,3,0,0);
        enable_video = 1;
        exchange(1,16384,3,16'h1234,0);
        exchange(1,16384,1,16'h00cd,0);
        exchange(0,16384,3,0,16'h12cd);
        exchange(1,16384,2,16'hab00,0);
        exchange(0,16384,3,0,16'habcd);
        exchange(1,32767,3,16'h9876,0);
        exchange(0,32767,3,0,16'h9876);
        // Cancel a write after WE assertion; the pin pulse/hold must still finish.
        @(negedge clk); cpu_request = 1; cpu_write = 1; cpu_address = 16385; cpu_lanes = 3; cpu_data = 16'hbeef;
        wait(!we); @(negedge clk); reset = 1; cpu_request = 0;
        repeat(8) @(negedge clk); reset = 0;
        exchange(0,16385,3,0,16'hbeef);
        exchange(0,16384,3,0,16'habcd);
        if(video_words < 5) $fatal(1,"Arbitration did not serve video");
        $display("PASS four-state SRAM pins: clear, byte lanes, boundary, concurrent DMA, reset during WE, retention");
        $finish;
    end
    initial begin #1000000; $fatal(1,"Memory timeout"); end
endmodule
