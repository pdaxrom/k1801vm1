`timescale 1ns/1ps
module tb_stop_probe;
reg clk=0,reset=1,stop=0;
always #5 clk=!clk;
wire [15:0] address,data;
wire [1:0] lanes;
wire cycle,strobe,writing,ack;
reg [15:0] memory[0:32767];
assign ack=cycle&&strobe;
bk_cpu cpu(.clk_p(clk),.clk_n(!clk),.reset(reset),.stop(stop),.virq(1'b0),
 .irq_vector(16'o100),.external_input(16'o177777),.system_input(16'o100370),
 .address(address),.write_data(data),.lanes(lanes),.cycle(cycle),.strobe(strobe),.write(writing),
 .read_data(memory[address[15:1]]),.acknowledge(ack));
integer i,trace=0;
always @(posedge clk) if(ack) begin
 if(writing) begin
  if(lanes[0])memory[address[15:1]][7:0]<=data[7:0];
  if(lanes[1])memory[address[15:1]][15:8]<=data[15:8];
 end
 if(stop&&trace<60) begin
  $display("%s %06o %06o pc=%06o psw=%06o",writing?"W":"R",address,writing?data:memory[address[15:1]],
   cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw); trace<=trace+1;
 end
end
initial begin
 for(i=0;i<32768;i=i+1)memory[i]=0;
 memory[16'o100000>>1]=16'o012706; memory[16'o100002>>1]=16'o001000;
 memory[16'o100004>>1]=16'o106427; memory[16'o100006>>1]=0;
 memory[16'o100010>>1]=16'o005237; memory[16'o100012>>1]=16'o000400;
 memory[16'o100014>>1]=16'o000775;
 memory[2]=16'o002000;memory[3]=16'o000340;
 memory[16'o002000>>1]=16'o106427;memory[16'o002002>>1]=16'o000340;
 memory[16'o002004>>1]=16'o005237;memory[16'o002006>>1]=16'o000402;
 memory[16'o002010>>1]=16'o000002;
 #100;reset=0;
 wait(memory[16'o400>>1]>=10);
 @(negedge clk);stop=1;
 #100000;stop=0;
 #10000;
 $display("PROBE counter=%0d STOP entries=%0d PC=%06o PSW=%06o",memory[16'o400>>1],memory[16'o402>>1],
 cpu.core.vreg_rm.vm1_vcram_reg.ram[1],cpu.core.psw);
 $finish;
end
initial begin #1000000; $fatal(1,"Stop probe timeout"); end
endmodule
