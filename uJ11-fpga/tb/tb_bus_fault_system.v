`timescale 1ns/1ps
// Real FRAM-system I/O-error path and odd-address guard. The handler performs
// an EA CALL, RTI unmasks a held IRQ, and its handler enters WAIT.
module tb_bus_fault_system;
    reg clk=0,reset=1,irq_valid=0;
    wire irq_ack,waiting,cs_n,sck,mosi,miso,io_request,io_write,io_byte;
    wire [15:0] io_address,io_wdata,ir,mdr,psw,q,rf_data;
    wire stopped,retire,rf_write,memory_request,memory_write,memory_ack;
    wire [1:0] fault;wire [3:0] rf_address;wire [9:0] upc;wire [35:0] uword;
    integer low_ipl,kind,waits,age=0,io_beats=0,irqs=0,retired=0,cycles,cases=0,cs_edges=0;
    reg [15:0] pc,opcode;reg [33:0] held_bus;reg held=0;
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
        if(reset)begin age<=0;held<=0;io_beats=0;irqs=0;retired=0;irq_valid<=0;cs_edges=0;end
        else begin
            if(held && (!io_request || held_bus!=={io_write,io_byte,io_address,io_wdata}))$fatal(1,"I/O fault request changed");
            held<=io_request&&!io_ack;held_bus<={io_write,io_byte,io_address,io_wdata};
            if(!io_request || io_ack)age<=0;else age<=age+1;
            if(io_ack)begin
                if(kind>=2 || io_beats || io_byte || io_address!==16'o177562 || io_write!==(kind==1))$fatal(1,"unexpected I/O fault beat");
                if(io_write && io_wdata!==16'h1234)$fatal(1,"I/O failed write value");
                io_beats=io_beats+1;
            end
            if(irq_ack)begin
                if(low_ipl ? (retired!=0 || ir!==16'o011203) : (retired!=1 || ir!==16'o000002))$fatal(1,"IRQ accepted before the expected handler instruction");
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
        for(low_ipl=0;low_ipl<2;low_ipl=low_ipl+1)for(kind=0;kind<4;kind=kind+1)for(waits=0;waits<4;waits=waits+1)begin
            @(negedge clk);reset=1;tick;@(negedge clk);reset=0;repeat(17)tick;
            @(negedge clk);
            pc=kind==3?16'h1001:16'h1000;opcode=kind==1?16'o010110:16'o011001;
            poke(pc,opcode);poke(4,16'h3000);poke(6,low_ipl?16'b0:16'o340);
            poke(16'h3000,16'o011203);poke(16'h3002,16'o000002);
            poke(16'o100,16'h4000);poke(16'o102,16'o340);poke(16'h4000,16'o000001);
            poke(16'h5000,16'hbeef);poke(16'h5ffc,16'hdead);poke(16'h5ffe,16'hdead);
            dut.core.engine.dp.rf.words[0]=kind==2?16'h2001:16'o177562;
            dut.core.engine.dp.rf.words[1]=16'h1234;dut.core.engine.dp.rf.words[2]=16'h5000;
            dut.core.engine.dp.rf.words[3]=16'h5678;dut.core.engine.dp.rf.words[6]=16'h6000;
            dut.core.engine.dp.rf.words[7]=pc;dut.core.engine.status.psw=3;irq_valid=1;
            cycles=0;
            while(!(upc==10'h020 && dut.core.engine.dp.rf.words[7]==16'h3000) && !stopped && cycles<2000)begin tick;cycles=cycles+1;end
            if(stopped || fault || psw!==(low_ipl?16'b0:16'o340) || retired || retire || irqs ||
               dut.core.engine.dp.rf.words[1]!==16'h1234 || dut.core.engine.dp.rf.words[3]!==16'h5678 ||
               dut.core.engine.dp.rf.words[6]!==16'h5ffc || peek(16'h5ffe)!==3 || peek(16'h5ffc)!==(kind==3?pc:pc+16'd2))
                $fatal(1,"first FRAM bus-fault frame kind%0d",kind);
            // Run through EA CALL, RTI and the delayed IRQ frame to WAIT.
            while(!waiting && !stopped && cycles<4500)begin tick;cycles=cycles+1;end
            tick;
            if(!waiting || stopped || fault || irqs!=1 || retired+(retire?1:0)!=(low_ipl?2:3) ||
               dut.core.engine.dp.rf.words[3]!==16'hbeef || dut.core.engine.dp.rf.words[7]!==16'h4002 ||
               dut.core.engine.dp.rf.words[6]!==(low_ipl?16'h5ff8:16'h5ffc) || psw!==16'o340 ||
               peek(low_ipl?16'h5ffa:16'h5ffe)!==(low_ipl?16'd8:16'd3) || peek(low_ipl?16'h5ff8:16'h5ffc)!==(low_ipl?16'h3002:kind==3?pc:pc+16'd2) || io_beats!=(kind<2?1:0))
                $fatal(1,"fault handler/RTI/IRQ continuation kind%0d retired%0d irq%0d",kind,retired,irqs);
            repeat(150)tick;cycles=cs_edges;repeat(100)tick;
            if(cs_edges!=cycles || retire || memory_request || io_request || !waiting)$fatal(1,"fault handler WAIT not quiescent");
            cases=cases+1;
        end
        $display("PASS bus fault system: %0d real FRAM I/O-error/odd-word frames, EA CALL, RTI, pending IRQ and quiescent WAIT cases",cases);$finish;
    end
    initial begin #10000000;$fatal(1,"fault system timeout");end
endmodule
