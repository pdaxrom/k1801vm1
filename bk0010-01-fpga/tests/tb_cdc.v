`timescale 1ns/1ps
module tb_cdc;
    reg cpu_clk = 0, sys_clk = 0, reset = 1;
    always #21 cpu_clk = !cpu_clk;
    always #17 sys_clk = !sys_clk;
    reg cycle = 0, strobe = 0, writing = 0;
    reg [15:0] address = 0, data = 0;
    reg [1:0] lanes = 0;
    wire ack, busy, request, bus_write;
    wire [15:0] result, bus_address, bus_data;
    wire [1:0] bus_lanes;
    reg ready = 0, enable = 1;
    reg [15:0] response = 0;
    reg [15:0] memory[0:255];
    integer i, delay = 0, transfers = 0;
    bk_wb_cdc bridge(cpu_clk, sys_clk, reset, cycle, strobe, writing, address, data, lanes,
                     ack, result, busy, request, bus_write, bus_address, bus_data, bus_lanes, ready, response,16'b0,);
    always @(posedge sys_clk) begin
        if(reset || !request) begin ready <= 0; delay <= 0; end
        else if(enable && !ready) begin
            delay <= delay + 1;
            if(delay == 4) begin
                ready <= 1; transfers <= transfers + 1;
                response <= memory[bus_address[7:0]];
                if(bus_write) begin
                    if(bus_lanes[0]) memory[bus_address[7:0]][7:0] <= bus_data[7:0];
                    if(bus_lanes[1]) memory[bus_address[7:0]][15:8] <= bus_data[15:8];
                end
            end
        end
    end
    task transfer(input wr, input [15:0] adr, value, input [1:0] mask, input [15:0] expected);
        begin
            wait(!busy); @(negedge cpu_clk);
            cycle = 1; strobe = 1; writing = wr; address = adr; data = value; lanes = mask;
            wait(ack); #1;
            if(!wr && result !== expected) $fatal(1, "CDC data %h expected %h", result, expected);
            repeat(3) begin @(negedge cpu_clk); if(!ack) $fatal(1, "ACK not held"); end
            strobe = 0; // Keep CYC asserted: read/modify/write strobe gap.
            @(negedge cpu_clk); wait(!busy);
        end
    endtask
    initial begin
        for(i=0;i<256;i=i+1) memory[i] = i;
        #150; reset = 0;
        transfer(0, 16'h11, 0, 3, 16'h11);
        transfer(1, 16'h11, 16'habcd, 1, 0);
        transfer(0, 16'h11, 0, 3, 16'h00cd);
        transfer(1, 16'h11, 16'hab00, 2, 0);
        transfer(0, 16'h11, 0, 3, 16'habcd);
        wait(!busy); @(negedge cpu_clk);
        enable = 0; address = 16'h22; strobe = 1;
        wait(request); repeat(12) @(negedge cpu_clk);
        strobe = 0; cycle = 0;
        wait(!busy); repeat(8) @(negedge cpu_clk);
        if(ack || request || transfers != 5) $fatal(1, "Timeout cancellation or duplicate access");
        enable = 1;
        transfer(0, 16'h33, 0, 3, 16'h33);
        @(negedge cpu_clk); cycle = 0;
        enable = 0; @(negedge cpu_clk); cycle = 1; strobe = 1;
        wait(request); reset = 1; #100; strobe = 0; cycle = 0; reset = 0; enable = 1;
        transfer(0, 16'h44, 0, 3, 16'h44);
        if(transfers != 7) $fatal(1, "Reset cancellation failed");
        $display("PASS CDC asynchronous byte lanes, CYC-preserving RMW, held ACK, timeout and reset");
        $finish;
    end
    initial begin #100000; $fatal(1, "CDC timeout"); end
endmodule
