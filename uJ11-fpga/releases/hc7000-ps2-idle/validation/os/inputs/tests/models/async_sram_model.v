`timescale 1ns/1ps
// IS61WV102416BLL-10T: tAA=10 ns, tHZOE=4 ns, tPWE=8 ns,
// tAW/tSCE/tPWB=8 ns, tSD=6 ns. Byte layout also supports existing runners.
module async_sram_model(
    input wire [19:0] address, inout wire [15:0] data,
    input wire ce_n, oe_n, we_n, lb_n, ub_n
);
    reg [7:0] memory[0:2097151];
    integer writes=0;
    wire reading=!ce_n && !oe_n && we_n;
    wire [15:0] read_value={memory[{address,1'b1}],memory[{address,1'b0}]};
`ifdef VERILATOR
    // This simulator cannot delay a tristate assignment; delay the value instead.
    // Turnaround/tHZ is exercised by the four-state Icarus unit test.
    wire [15:0] delayed_value;
    assign #10 delayed_value=read_value;
    assign data=reading ? delayed_value : 16'bz;
`else
    assign #(10,10,4) data[7:0]=reading && !lb_n ? read_value[7:0] : 8'bz;
    assign #(10,10,4) data[15:8]=reading && !ub_n ? read_value[15:8] : 8'bz;
`endif
    reg writing=0;
    realtime began, addr_changed=0, data_changed=0, selected=0, lanes_changed=0;
    always @(address) addr_changed=$realtime;
    always @(data) data_changed=$realtime;
    always @(negedge ce_n) selected=$realtime;
    always @(lb_n or ub_n) lanes_changed=$realtime;
    always @(negedge we_n) if(!ce_n) begin
        if(!oe_n) $fatal(1,"SRAM output enabled during write");
        writing=1; began=$realtime;
    end
    always @(posedge we_n or posedge ce_n) if(writing) begin
        if($realtime-began<8 || $realtime-addr_changed<8 ||
           $realtime-selected<8 || $realtime-lanes_changed<8 || $realtime-data_changed<6)
            $fatal(1,"SRAM write timing violation");
        if(!lb_n) memory[{address,1'b0}]=data[7:0];
        if(!ub_n) memory[{address,1'b1}]=data[15:8];
        writes=writes+1; writing=0;
    end
endmodule
