`timescale 1ns/1ps
module tb_d_input_cp42;
reg [35:0] uword=0;
reg [15:0] ir=0,mdr=0,psw=0,apr_data=0;
reg [3:0] a=0;
reg byte_instruction=0,mmu_active=0;
wire [15:0] gd,ge,dd,de;
d_input_reference gold(uword,ir,mdr,psw,apr_data,a,byte_instruction,mmu_active,gd,ge);
d_input gate(uword,ir,mdr,psw,apr_data,a,byte_instruction,mmu_active,dd,de);
reg [31:0] rng=32'h42c0ffee;
function [31:0] advance;
input [31:0] old;
reg [31:0] x;
begin x=old^(old<<13);x=x^(x>>17);advance=x^(x<<5);end
endfunction
integer i,j,n=0;
task compare;
begin
 #1;
 if ({gd,ge} !== {dd,de}) $fatal(1,"D mismatch %0d sel=%d a=%h IR=%h gold=%h/%h gate=%h/%h",n,uword[12:10],a,ir,gd,ge,dd,de);
 n=n+1;
end
endtask
initial begin
 for(i=0;i<131072;i=i+1)begin
  rng=advance(rng);uword={rng[3:0],rng};
  uword[12:10]=i[2:0];a=i[6:3];byte_instruction=i[7];mmu_active=i[8];uword[7:0]=i[16:9];
  ir=i[15:0];rng=advance(rng);mdr=rng[15:0];psw=rng[31:16];rng=advance(rng);apr_data=rng[15:0];
  compare();
 end
 // Unknown selected and unselected data. Selectors remain known: an X selector
 // in the historical case statement is outside this combinational contract.
 for(i=0;i<512;i=i+1)begin
  uword=0;uword[12:10]=i[2:0];a=i[6:3];byte_instruction=i[7];mmu_active=i[8];
  for(j=0;j<8;j=j+1)begin
   ir=16'h8180;mdr=16'h3412;psw=16'ha55a;apr_data=16'hcdef;uword[7:0]=8'h80;
   case(j)
    0:ir=16'hxxxx;
    1:mdr=16'hxxxx;
    2:psw=16'hxxxx;
    3:apr_data=16'hxxxx;
    4:uword[7:0]=8'hxx;
    5:begin ir=16'hxxxx;mdr=16'hxxxx;psw=16'hxxxx;apr_data=16'hxxxx;end
    6:begin a=4'hx;end
    7:begin byte_instruction=1'bx;end
   endcase
   compare();a=i[6:3];byte_instruction=i[7];
  end
 end
 $display("PASS CP42 D-input: %0d comparisons, 131072 known vectors + 4096 four-state vectors",n);
 $finish;
end
endmodule
