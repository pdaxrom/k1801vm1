`timescale 1ns/1ps
// Check actual SERV writes against the ELF layout, including the stack guard.
module serv_memory_guard(input wire clk,reset,write,input wire [31:0] address);
    integer bss_start,bss_end,stack_bottom,stack_top,lowest_stack;
    initial begin
        if(!$value$plusargs("IOP_BSS_START=%h",bss_start) || !$value$plusargs("IOP_BSS_END=%h",bss_end) ||
           !$value$plusargs("IOP_STACK_BOTTOM=%h",stack_bottom) || !$value$plusargs("IOP_STACK_TOP=%h",stack_top))
            $fatal(1,"SERV memory bounds required");
        lowest_stack=stack_top;
    end
    always @(posedge clk)if(!reset && write)begin
        if(!((address>=bss_start && address<bss_end) || (address>=stack_bottom && address<stack_top)))
            $fatal(1,"SERV write outside data/stack: %h; data %h..%h, stack %h..%h",address,bss_start,bss_end,stack_bottom,stack_top);
        if(address>=stack_bottom && address<lowest_stack)lowest_stack=address;
    end
    final $display("SERV stack writes: %0d / %0d reserved bytes",stack_top-lowest_stack,stack_top-stack_bottom);
endmodule
