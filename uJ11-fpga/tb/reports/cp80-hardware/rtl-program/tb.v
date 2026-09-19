`timescale 1ns/1ps
// The unchanged CP67 CPU executes an assembled PDP-11 module. Only external
// memory fixtures are initialized here; no RF/service/FPS RTL state is forced.
module tb_fp_psw_cp80 #(parameter integer ROM_DECODE=1);
    reg clk=0,reset=1,halt_button=0,irq_valid=0;
    wire [15:0] address,wdata,rdata,psw,ir;
    wire request,writing,byte_access,bank,physical,irq_ack,waiting,stopped;
    wire [1:0] fault;
    wire [9:0] upc;
    reg [15:0] memory[0:65535];
    integer cycles=0,beats=0,checks=0,scenario=0,wait_count=0,delay_cycles=0;
    reg inject_error=0,error_bank=0,allow_stopped=0;
    reg [15:0] error_address=0;
    wire ack=request && wait_count==delay_cycles;
    wire error=inject_error && bank==error_bank && address==error_address;
    assign rdata=memory[{bank,address[15:1]}];
    uj11_core #(.ROM_DECODE(ROM_DECODE),.IRQ_VECTOR_BITS(15),.UNMASKED_VECTOR(16'o160000)) dut(
        .clk(clk),.reset(reset),.halt_button(halt_button),.debug_block(1'b0),
        .irq_valid(irq_valid),.irq_priority(3'd7),.irq_vector(15'o100),.irq_ack(irq_ack),
        .waiting(waiting),.peripheral_reset(),.mem_addr(address),.mem_write_data(wdata),
        .mem_request(request),.mem_read(),.mem_write(writing),.mem_byte(byte_access),
        .mem_bank(bank),.mem_physical(physical),.mem_ack(ack),.mem_error(error),
        .mem_read_data(rdata),.stopped(stopped),.fault_code(fault),.retire(),
        .debug_upc(upc),.debug_uword(),.ir(ir),.mdr(),.psw(psw),.q(),
        .debug_rf_write(),.debug_rf_address(),.debug_rf_data());
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge clk) if(reset) wait_count<=0; else begin
        cycles<=cycles+1;
        if(request && !ack)wait_count<=wait_count+1;else wait_count<=0;
        if(ack)begin
            beats<=beats+1;
            if(writing && !error)begin
                if(byte_access)$fatal(1,"Unexpected byte write");
                memory[{bank,address[15:1]}]<=wdata;
            end
        end
        if(irq_ack)begin
            if(dut.engine.service_mode)$fatal(1,"IRQ inside FP11 service");
            irq_valid<=0;
        end
        if(cycles>200000 || (stopped && !allow_stopped))
            $fatal(1,"case%0d stopped/timeout PC%o IR%o uPC%h",scenario,dut.engine.dp.rf.words[7],ir,upc);
    end
    task eq(input [15:0] got,want,input string what);
        begin
            checks=checks+1;
            if(got!==want)$fatal(1,"case%0d %s got%o want%o PC%o IR%o uPC%h",scenario,what,got,want,dut.engine.dp.rf.words[7],ir,upc);
        end
    endtask
    `include "fp80_symbols.vh"
    reg [15:0] values[0:22];
    reg [15:0] guest_start=16'o1000;
    integer vecfile,rc,stride=1,total=0,executed=0,start_clocks,start_beats,metrics;
    reg [4095:0] measured=0;
    string vectors,metric_path;
    task prepare;
        begin
            @(negedge clk);reset=1;irq_valid=0;halt_button=0;inject_error=0;allow_stopped=0;
            repeat(4)@(negedge clk);cycles=0;beats=0;
            memory[32768]=16'o2400;memory[32769]=16'o340;
            memory[32770]=16'o312;memory[32771]=16'o340;
            memory[32768+16'o6000/2]=values[1];
            memory[32768+16'o6002/2]=values[2] & ~16'o20;
            for(integer r=0;r<7;r++)memory[32768+16'o6100/2+r]=values[3+r];
            memory[32768+16'o6006/2]=guest_start;
            memory[guest_start>>1]=values[0];
            memory[(16'(guest_start+2)>>1)]=16'o777;
            // Enter a T-set test with architectural RTT suppression; START
            // itself checks T before fetching, so it cannot seed this state.
            if(values[2][4])begin
                memory[(16'(guest_start+0)>>1)]=16'o6; // RTT
                memory[(16'(guest_start+2)>>1)]=values[0];
                memory[(16'(guest_start+4)>>1)]=16'o777;
                memory[32768+16'o6100/2+6]=values[9]-4;
                memory[(values[9]-16'd4)>>1]=guest_start+2;
                memory[(values[9]-16'd2)>>1]=values[2];
            end
            memory[16'o177776/2]=16'hdead;
            @(negedge clk);reset=0;
            wait(!dut.engine.service_mode);@(negedge clk);
            eq(memory[32768+16'o6004/2],0,"cold initializer result");
            eq(dut.engine.service_ready,3,"FP preserves ODT ready");
            eq(dut.engine.debug_enabled,1,"FP preserves debug enable");
            for(integer a=0;a<24;a++)begin
                eq(memory[32768+FP_ACS/2+a],0,"cold initializes every AC word");
                memory[32768+FP_ACS/2+a]=16'h8100+a;
            end
            start_clocks=cycles;start_beats=beats;
        end
    endtask
    task finish_fp;
        begin wait(dut.engine.service_mode);wait(!dut.engine.service_mode);@(negedge clk);end
    endtask
    always @(posedge clk) if(!reset && request && !bank && address==16'o177776)
        $fatal(1,"PSW operand escaped to bus");
    initial begin
        for(integer n=0;n<65536;n++)memory[n]=0;
        $readmemh("build/cp80-fp11/psw-sync/image.mem",memory);
        for(integer n=0;n<23;n++)values[n]=0;
        values[0]=16'o170127;values[2]=16'o340;values[9]=16'o30000;
        guest_start=16'o6000;prepare();
        memory[1536]=16'o170127;
        memory[1537]=16'o0;
        memory[1538]=16'o172427;
        memory[1539]=16'o40200;
        memory[1540]=16'o175437;
        memory[1541]=16'o177776;
        memory[1542]=16'o170137;
        memory[1543]=16'o177776;
        memory[1544]=16'o170200;
        memory[1545]=16'o175037;
        memory[1546]=16'o177776;
        memory[1547]=16'o172427;
        memory[1548]=16'o41200;
        memory[1549]=16'o175437;
        memory[1550]=16'o177776;
        memory[1551]=16'o172427;
        memory[1552]=16'o40200;
        memory[1553]=16'o170700;
        memory[1554]=16'o175437;
        memory[1555]=16'o177776;
        memory[1556]=16'o170137;
        memory[1557]=16'o177776;
        memory[1558]=16'o170201;
        memory[1559]=16'o170127;
        memory[1560]=16'o0;
        memory[1561]=16'o170400;
        memory[1562]=16'o175437;
        memory[1563]=16'o177776;
        memory[1564]=16'o170137;
        memory[1565]=16'o177776;
        memory[1566]=16'o170202;
        memory[1567]=16'o170127;
        memory[1568]=16'o3000;
        memory[1569]=16'o170237;
        memory[1570]=16'o177776;
        memory[1571]=16'o170137;
        memory[1572]=16'o177776;
        memory[1573]=16'o170203;
        memory[1574]=16'o777;
        scenario=1;finish_fp();
        eq(psw,16'o340,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6004,"program PC");
        scenario=2;finish_fp();
        eq(psw,16'o340,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6010,"program PC");
        scenario=3;finish_fp();
        eq(psw,16'o1,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6014,"program PC");
        scenario=4;finish_fp();
        eq(psw,16'o1,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o1,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6020,"program PC");
        scenario=5;finish_fp();
        eq(psw,16'o1,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o1,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6022,"program PC");
        eq(dut.engine.dp.rf.words[0],16'o1,"program register");
        scenario=6;finish_fp();
        eq(psw,16'o1,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6026,"program PC");
        scenario=7;finish_fp();
        eq(psw,16'o1,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6032,"program PC");
        scenario=8;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6036,"program PC");
        scenario=9;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6042,"program PC");
        scenario=10;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o10,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6044,"program PC");
        scenario=11;finish_fp();
        eq(psw,16'o174757,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o10,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6050,"program PC");
        scenario=12;finish_fp();
        eq(psw,16'o174757,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o144757,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6054,"program PC");
        scenario=13;finish_fp();
        eq(psw,16'o174757,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o144757,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6056,"program PC");
        eq(dut.engine.dp.rf.words[1],16'o144757,"program register");
        scenario=14;finish_fp();
        eq(psw,16'o174757,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6062,"program PC");
        scenario=15;finish_fp();
        eq(psw,16'o174757,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o4,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6064,"program PC");
        scenario=16;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o4,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6070,"program PC");
        scenario=17;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6074,"program PC");
        scenario=18;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6076,"program PC");
        eq(dut.engine.dp.rf.words[2],16'o0,"program register");
        scenario=19;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o3000,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6102,"program PC");
        scenario=20;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o3000,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6106,"program PC");
        scenario=21;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6112,"program PC");
        scenario=22;finish_fp();
        eq(psw,16'o0,"program PSW");
        eq(memory[32768+FP_FPS/2],16'o0,"program FPS");
        eq(dut.engine.dp.rf.words[7],16'o6114,"program PC");
        eq(dut.engine.dp.rf.words[3],16'o0,"program register");
        $display("PASS CP80 hardware program: %0d instructions / %0d checks",scenario,checks);
        $finish;
    end
endmodule
