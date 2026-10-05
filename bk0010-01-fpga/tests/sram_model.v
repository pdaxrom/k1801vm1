`timescale 1ns/1ps
// Pin-level asynchronous 10 ns SRAM; checks complete write cycles and lanes.
module sram_model (
    input wire [19:0] address,
    inout wire [15:0] data,
    input wire ce_n, oe_n, we_n, lb_n, ub_n
);
    reg [15:0] memory[0:131071];
    reg [19:0] write_address;
    reg [15:0] write_data;
    reg [1:0] write_lanes;
    realtime write_start;
    integer writes = 0;
    wire reading = !ce_n && !oe_n && we_n;
`ifdef VERILATOR
    // This simulator cannot delay a tristate; delay the SRAM value before enabling DQ.
    wire [15:0] delayed_data;
    assign #10 delayed_data = memory[address[16:0]];
    assign data = reading ? delayed_data : 16'bz;
`else
    assign #10 data[7:0] = reading && !lb_n ? memory[address[16:0]][7:0] : 8'bz;
    assign #10 data[15:8] = reading && !ub_n ? memory[address[16:0]][15:8] : 8'bz;
`endif
    always @(negedge we_n) if(!ce_n) begin
        write_start = $realtime; write_address = address;
        write_data = data; write_lanes = {!ub_n,!lb_n};
        if(!oe_n || address >= 131072) $fatal(1,"SRAM write contention/address");
    end
    always @(posedge we_n) if(!ce_n) begin
        if($realtime-write_start < 8 || address != write_address || data !== write_data ||
            {!ub_n,!lb_n} != write_lanes) $fatal(1,"SRAM write timing/stability");
        if(write_lanes[0]) memory[address[16:0]][7:0] = data[7:0];
        if(write_lanes[1]) memory[address[16:0]][15:8] = data[15:8];
        writes = writes + 1;
    end
endmodule
