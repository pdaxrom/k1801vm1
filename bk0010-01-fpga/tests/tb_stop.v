`timescale 1ns/1ps
// BK STOP must escape an IRQ100 handler even with PSW[7] set. The missing
// console-save reply, native timeout and vector 4 are all executed by VM1.
module tb_stop;
    reg clk48 = 0, reset = 1, stop = 0, virq = 0;
    always #10.416667 clk48 = !clk48;
    reg [2:0] mode = 0;
    wire clk, clk_n, stalled;
    wire [2:0] active;
    wire [15:0] address, data;
    wire [1:0] lanes;
    wire cycle, strobe, writing, irq_ack;
    reg [15:0] memory[0:32767];
    wire ack = cycle && strobe;
    bk_cpu_clock clocks(clk48,reset,mode,1'b1,clk,clk_n,active,stalled);
    bk_cpu cpu(.clk_p(clk),.clk_n(clk_n),.reset(reset),.stop(stop),.virq(virq),
        .irq_vector(16'o100),.external_input(16'o177777),.system_input(16'o100370),
        .address(address),.write_data(data),.lanes(lanes),.cycle(cycle),.strobe(strobe),.write(writing),
        .read_data(memory[address[15:1]]),.acknowledge(ack),.irq_ack(irq_ack));
    integer frequency, i;
    always @(posedge clk) if(ack && writing) begin
        if(lanes[0]) memory[address[15:1]][7:0] <= data[7:0];
        if(lanes[1]) memory[address[15:1]][15:8] <= data[15:8];
    end
    task word_at(input [15:0] target, value);
        begin memory[target >> 1] = value; end
    endtask
    task prepare_program;
        begin
            for(i=0;i<32768;i=i+1) memory[i] = 0;
            word_at(16'o100000,16'o012706); word_at(16'o100002,16'o004000); // MOV #4000,SP
            word_at(16'o100004,16'o106427); word_at(16'o100006,0);          // MTPS #0
            word_at(16'o100010,16'o005237); word_at(16'o100012,16'o400);    // INC @#400
            word_at(16'o100014,16'o000775); word_at(16'o100016,16'o000774);// BR idle
            word_at(4,16'o2000); word_at(6,16'o340);
            word_at(16'o100,16'o3000); word_at(16'o102,16'o340);
            word_at(16'o2000,16'o106427); word_at(16'o2002,16'o340);       // MTPS #340
            word_at(16'o2004,16'o005237); word_at(16'o2006,16'o402);       // INC @#402
            word_at(16'o2010,16'o013737); word_at(16'o2012,16'o177716);   // MOV flag,@#404
            word_at(16'o2014,16'o404);
            word_at(16'o2016,16'o013737); word_at(16'o2020,16'o177716);   // Read-to-clear
            word_at(16'o2022,16'o406); word_at(16'o2024,16'o000002);      // RTI
            word_at(16'o3000,16'o005237); word_at(16'o3002,16'o410);      // INC @#410
            word_at(16'o3004,16'o012700); word_at(16'o3006,16'o3000);     // MOV #3000,R0
            word_at(16'o3010,16'o077001);                               // SOB R0,3010
            word_at(16'o3012,16'o005237); word_at(16'o3014,16'o412);      // INC @#412
            word_at(16'o3016,16'o000002);                               // RTI
        end
    endtask
    task check_flag;
        begin
            if(!memory[16'o404 >> 1][2] || memory[16'o406 >> 1][2])
                $fatal(1,"STOP/HALT reason flag did not read then clear, rate=%0d",mode);
        end
    endtask
    initial begin
        for(frequency=0;frequency<7;frequency=frequency+1) begin
            reset = 1; stop = 0; virq = 0; mode = frequency; prepare_program();
            repeat(20) @(negedge clk48);
            reset = 0;
            wait(memory[16'o400 >> 1] >= 10);
            @(negedge clk); virq = 1;
            wait(irq_ack); @(negedge clk); virq = 0;
            wait(memory[16'o410 >> 1] == 1); // IRQ100 has loaded PSW=340.
            @(negedge clk); stop = 1;
            #120000000;
            if(memory[16'o402 >> 1] != 1 || memory[16'o412 >> 1] != 1)
                $fatal(1,"Held STOP failed/repeated or damaged IRQ100 return, rate=%0d count=%0d",mode,
                       memory[16'o402 >> 1]);
            check_flag();
            @(negedge clk); stop = 0;
            repeat(100) @(negedge clk);
            stop = 1; #1000; stop = 0; // Short press survives until VM1 accepts it.
            wait(memory[16'o402 >> 1] == 2);
            repeat(500) @(negedge clk); check_flag();
            // HALT follows the same absent console-save register path.
            word_at(16'o100014,0);
            wait(memory[16'o402 >> 1] == 3);
            word_at(16'o100014,16'o000775);
            repeat(500) @(negedge clk); check_flag();
            if(memory[16'o402 >> 1] != 3 || cpu.core.vreg_rm.vm1_vcram_reg.ram[9] != 16'o4000)
                $fatal(1,"STOP/HALT did not resume with intact stack, rate=%0d",mode);
            $display("PASS rate=%0d: STOP inside IRQ100, 120 ms hold, short press, HALT, vector 4, reason flag, RTI stack",mode);
        end
        $finish;
    end
    initial begin #1100000000; $fatal(1,"STOP regression timeout rate=%0d",mode); end
endmodule
