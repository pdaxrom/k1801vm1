`timescale 1ns/1ps
// Exhaust each bit's data inputs under every control combination. PSW logic
// has no arithmetic or cross-bit reduction; high byte selects bus bit b-8.
module tb_psw_factor_cp78;
    reg clk=0,reset=0,enable=0,bus_write=0,bus_byte=0,bus_odd=0;
    reg [1:0] update=0;
    reg [3:0] nzvc=0;
    reg [15:0] value=0,bus_value=0;
    wire [15:0] gold,gate;
    integer checks=0;
    uj11_psw_gold reference(.clk(clk),.reset(reset),.enable(enable),.update(update),.nzvc(nzvc),
        .value(value),.bus_write(bus_write),.bus_byte(bus_byte),.bus_odd(bus_odd),.bus_value(bus_value),.psw(gold));
    uj11_psw candidate(.clk(clk),.reset(reset),.enable(enable),.update(update),.nzvc(nzvc),
        .value(value),.bus_write(bus_write),.bus_byte(bus_byte),.bus_odd(bus_odd),.bus_value(bus_value),.psw(gate));
    task edge_check;
        #1;clk=1;#1;
        if(gold!==gate)$fatal(1,"PSW mismatch old/new %h/%h ctl %b%b%b%b%b%b data %h/%h/%h",gold,gate,reset,enable,update,bus_write,bus_byte,bus_odd,value,bus_value,nzvc);
        checks++;clk=0;#1;
    endtask
    initial begin
        reset=1;edge_check();reset=0;
        for(integer b=0;b<16;b++)
          for(integer c=0;c<128;c++)
            for(integer d=0;d<32;d++)begin
                // Install any relevant reachable previous bit via architectural LOAD.
                reset=0;enable=1;bus_write=0;update=3;
                value=d[0] ? 16'hffff : 0;edge_check();
                {reset,enable,update,bus_write,bus_byte,bus_odd}=7'(c);
                value=d[1] ? 16'hffff : 0;
                nzvc=d[2] ? 4'hf : 0;
                bus_value=0;bus_value[b]=d[3];
                if(b>=8)bus_value[b-8]=d[4];
                edge_check();
            end
        // Excluded/unused operands may be X in four-state RTL simulation.
        reset=0;enable=1;bus_write=0;update=3;value=16'hf9ff;edge_check();
        update=1;nzvc=4'b010x;value=16'hxxxx;bus_value=16'hxxxx;edge_check();
        update=0;nzvc=4'bxxxx;edge_check();
        bus_write=1;bus_byte=1;bus_odd=1;bus_value=16'hxxa5;edge_check();
        bus_odd=0;bus_value=16'hxx3f;edge_check();
        $display("PASS CP78 PSW factor equivalence: %0d clock comparisons",checks);$finish;
    end
endmodule
