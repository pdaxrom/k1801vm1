`timescale 1ns/1ps
// Multiple instructions through the real SPI FRAM/I/O system. Trace frames
// must defer held IRQ until a handler instruction; RTT must trace its successor.
module tb_trace_system;
 reg clk=0,reset=1,irq_valid=0;
 wire irq_ack,waiting,cs_n,sck,mosi,miso,io_request,io_write,io_byte;
 wire [15:0] io_address,io_wdata,ir,mdr,psw,q,rf_data;
 wire stopped,retire,rf_write,memory_request,memory_write,memory_ack;
 wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
 integer kind,waits,age=0,io_beats=0,irqs=0,retired=0,traces=0,cycles,cases=0,cs_edges=0;
 integer expected_retired,expected_traces;
 reg [15:0] first_sp,first_psw,first_pc,last_sp;
 wire io_ack=io_request && age>=waits;
 uj11_fram_system dut(.clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(3'd7),
  .irq_vector(8'o040),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),.spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso),
  .io_request(io_request),.io_write(io_write),.io_byte(io_byte),.io_address(io_address),.io_wdata(io_wdata),
  .io_rdata(16'hdead),.io_ack(io_ack),.io_error(1'b1),.stopped(stopped),.retire(retire),.fault_code(fault),
  .debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
  .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data),
  .memory_request(memory_request),.memory_write(memory_write),.memory_ack(memory_ack));
 spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
`ifdef VENDOR_ROM
 GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
 always #5 clk=~clk;
 always @(negedge cs_n)if(!reset)cs_edges=cs_edges+1;
 always @(posedge clk)begin
  if(reset)begin age<=0;io_beats=0;irqs=0;retired=0;traces=0;irq_valid<=0;cs_edges=0;end
  else begin
   if(!io_request || io_ack)age<=0;else age<=age+1;
   if(io_ack)begin
    if(kind!=3 || io_beats || io_byte || io_address!==16'o177562 || io_write)$fatal(1,"unexpected trace I/O");
    io_beats=io_beats+1;
   end
   if(dut.core.engine.trace_ack)begin
    if(irq_ack || (kind==1 && traces==0 && ir!==16'o011203) ||
       (kind==5 && traces==0 && ir!==16'o000002))$fatal(1,"wrong return/IRQ trace boundary");
    traces=traces+1;
   end
   if(irq_ack)begin
    if(kind==4 || ir!==16'o011203 || (!rf_write || rf_address!=3 || rf_data!==16'hbeef) ||
       retired!=(kind==1?2:kind==3?0:1))$fatal(1,"IRQ before trace/fault handler instruction kind%0d retires%0d",kind,retired);
    irqs=irqs+1;irq_valid<=0;
   end
   if(retire)retired=retired+1;
  end
 end
 task tick;begin @(posedge clk);#1;end endtask
 task poke;input [15:0] a,v;begin fram.memory[a]=v[7:0];fram.memory[a+16'd1]=v[15:8];end endtask
 function [15:0] peek;input [15:0] a;begin peek={fram.memory[a+16'd1],fram.memory[a]};end endfunction
 initial begin
  #100;
  for(kind=0;kind<6;kind=kind+1)for(waits=0;waits<4;waits=waits+1)begin
   @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;@(negedge clk);
   poke(16'h1000,kind==1?16'o000006:kind==2?16'o000001:kind==3?16'o011001:kind==5?16'o000002:16'o060001);
   poke(16'h1800,16'o011203);poke(16'h3000,16'o011203);poke(16'h3002,16'o000002);
   poke(4,16'h3000);poke(6,0);poke(16'o14,16'h3000);poke(16'o16,kind==4?16'h10:0);
   poke(16'o100,16'h4000);poke(16'o102,16'o340);poke(16'h4000,16'o000001);
   poke(16'h5000,16'hbeef);poke(16'h6000,16'h1800);poke(16'h6002,kind==1?16'hf3:16'h13);
   dut.core.engine.dp.rf.words[0]=kind==3?16'o177562:16'd1;
   dut.core.engine.dp.rf.words[1]=16'hffff;dut.core.engine.dp.rf.words[2]=16'h5000;
   dut.core.engine.dp.rf.words[3]=16'h5678;dut.core.engine.dp.rf.words[6]=16'h6000;
   dut.core.engine.dp.rf.words[7]=16'h1000;dut.core.engine.status.psw=kind==5?16'h3:16'h13;
   // RTT restores IPL7; other cases offer a held IRQ immediately, except
   // WAIT: assert it after WAIT's first retirement so trace wins that edge.
   irq_valid=(kind!=2 && kind!=1);
   first_sp=(kind==1 || kind==5)?16'h6000:16'h5ffc;
   first_pc=kind==1?16'h1802:kind==5?16'h1800:16'h1002;
   first_psw=kind==1?16'hf9:(kind==0 || kind==4)?16'h15:16'h13;
   cycles=0;
   if(kind==2)begin
    while(!retire && !stopped && cycles<500)begin tick;cycles=cycles+1;end
    if(!waiting)$fatal(1,"WAIT trace retired at wrong state");
    @(negedge clk);irq_valid=1;
   end
   if(kind==1)begin
    while(!retire && !stopped && cycles<800)begin tick;cycles=cycles+1;end
    if(traces || upc!==10'h020 || dut.core.engine.dp.rf.words[7]!==16'h1800 || psw!==16'hf3)$fatal(1,"RTT did not suppress own trace");
    @(negedge clk);irq_valid=1;
   end
   while(!(upc==10'h020 && dut.core.engine.dp.rf.words[7]==16'h3000) && !stopped && cycles<2500)begin tick;cycles=cycles+1;end
   if(stopped || fault || irqs || traces!=(kind==3?0:1) || dut.core.engine.dp.rf.words[6]!==first_sp ||
      peek(first_sp)!==first_pc || peek(first_sp+16'd2)!==first_psw || psw!==(kind==4?16'h10:0))
    $fatal(1,"first trace/fault frame kind%0d sp%h psw%h frame%h/%h traces%0d",kind,dut.core.engine.dp.rf.words[6],psw,peek(first_sp),peek(first_sp+16'd2),traces);
   if(kind!=1 && dut.core.engine.dp.rf.words[3]!==16'h5678)$fatal(1,"target instruction ran before trace");
   if(kind==4)begin
    tick;
    while(!(upc==10'h020 && dut.core.engine.dp.rf.words[7]==16'h3000 && traces==2) && !stopped && cycles<4500)begin tick;cycles=cycles+1;end
    if(stopped || irqs || traces!=2 || retired+(retire?1:0)!=2 || psw!==16'h10 ||
       dut.core.engine.dp.rf.words[6]!==16'h5ff8 || peek(16'h5ff8)!==16'h3002 || peek(16'h5ffa)!==16'h18)
     $fatal(1,"vector T did not trace handler instruction");
   end else begin
    while(!waiting && !stopped && cycles<4500)begin tick;cycles=cycles+1;end
    tick;expected_retired=kind==1?4:kind==3?2:3;expected_traces=kind==3?0:1;last_sp=first_sp-16'd4;
    if(!waiting || stopped || fault || irqs!=1 || traces!=expected_traces || retired+(retire?1:0)!=expected_retired ||
       dut.core.engine.dp.rf.words[3]!==16'hbeef || dut.core.engine.dp.rf.words[7]!==16'h4002 ||
       dut.core.engine.dp.rf.words[6]!==last_sp || psw!==16'o340 ||
       peek(last_sp)!==16'h3002 || peek(last_sp+16'd2)!==16'h8 || io_beats!=(kind==3?1:0))
     $fatal(1,"trace continuation kind%0d retired%0d IRQ%0d traces%0d",kind,retired,irqs,traces);
    repeat(150)tick;cycles=cs_edges;repeat(100)tick;
    if(cs_edges!=cycles || retire || memory_request || io_request || !waiting)$fatal(1,"trace handler WAIT not quiescent");
   end
   cases=cases+1;
  end
  $display("PASS trace system: %0d real FRAM trace/RTI/RTT/WAIT/IRQ and I/O-fault cases",cases);$finish;
 end
 initial begin #10000000;$fatal(1,"trace system timeout");end
endmodule
