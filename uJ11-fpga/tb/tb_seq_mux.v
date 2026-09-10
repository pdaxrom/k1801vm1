`timescale 1ns/1ps
module tb_seq_mux;
reg clk=0,reset=1,enable=0;
reg [35:0] uword=0;
reg [15:0] ir=0;
reg [3:0] nzvc=0,selected_a=0;
reg [9:0] dispatch_address=0,context_address=0;
reg address_odd=0,byte_instruction=0,q0=0,loop_zero=0,bus_error=0,a_one=0;
reg irq_pending=0,trace_pending=0,fault_redirect=0,fault_repair=0,context_redirect=0;
wire [9:0] gu,gn,du,dn;
`define PORTS .clk(clk),.reset(reset),.enable(enable),.uword(uword),.ir(ir),.nzvc(nzvc),.dispatch_address(dispatch_address),.address_odd(address_odd),.byte_instruction(byte_instruction),.selected_a(selected_a),.q0(q0),.loop_zero(loop_zero),.bus_error(bus_error),.a_one(a_one),.irq_pending(irq_pending),.trace_pending(trace_pending),.fault_redirect(fault_redirect),.fault_repair(fault_repair)
`ifdef APR
uj11_microseq_reference gold(`PORTS,.context_redirect(context_redirect),.context_address(context_address),.upc(gu),.next_address(gn));
uj11_microseq gate(`PORTS,.context_redirect(context_redirect),.context_address(context_address),.upc(du),.next_address(dn));
`else
uj11_microseq_reference gold(`PORTS,.upc(gu),.next_address(gn));
uj11_microseq gate(`PORTS,.upc(du),.next_address(dn));
`endif
reg [31:0] rng=32'h41c0ffee;
function [31:0] random_word;
input [31:0] old;
reg [31:0] x;
begin x=old^(old<<13);x=x^(x>>17);random_word=x^(x<<5);end
endfunction
integer i,checks=0;
task compare;
begin
 if ({gu,gn,gold.link,gold.link_valid} !== {du,dn,gate.link,gate.link_valid})
  $fatal(1,"sequencer mismatch i=%0d word=%h gold=%h/%h gate=%h/%h",i,uword,gu,gn,du,dn);
 checks=checks+1;
end
endtask
initial begin
 #1;clk=1;#1;compare();clk=0;
 for(i=0;i<131072;i=i+1)begin
  rng=random_word(rng);uword={rng[3:0],rng};
  // First half exhausts command/control and low eleven word bits. Remaining
  // inputs and the second half use a reproducible xorshift stream.
  if(i<65536)begin uword[35:31]=i[15:11];uword[10:0]=i[10:0];end
  rng=random_word(rng);ir=rng[15:0];nzvc=rng[19:16];selected_a=rng[23:20];
  rng=random_word(rng);dispatch_address=rng[9:0];context_address=rng[19:10];
  {address_odd,byte_instruction,q0,loop_zero,bus_error,a_one,irq_pending,trace_pending,fault_redirect,fault_repair,context_redirect}=rng[30:20];
  enable=i[1:0]!=0;reset=i%257==0;
  // Matched arbitrary states test wrap, live link and invalid RETURN without
  // relying on a software path to reach each case. Other cycles evolve normally.
  if(i%64==1)begin
   gold.upc=rng[9:0];gate.upc=rng[9:0];
   gold.link=rng[19:10];gate.link=rng[19:10];
   gold.link_valid=i[6];gate.link_valid=i[6];
  end
  #1;compare();clk=1;#1;compare();clk=0;
 end
 $display("PASS CP41 sequencer: 131072 cycles, %0d comparisons, all control/command/low-word combinations",checks);
 $finish;
end
endmodule
