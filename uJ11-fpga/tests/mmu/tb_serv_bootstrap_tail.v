`timescale 1ns/1ps
// Execute real SERV RV32IC fetches across the 16/18 KiB bank boundaries.
module tb_serv_bootstrap_tail #(parameter integer CLOCK_HZ=50000000, parameter integer BOOT_ADDRESS=18*1024-2);
    reg clk=0,reset=1;
    always #(500000000.0/CLOCK_HZ) clk=~clk;
    wire cpu_start;
    uj11_mmu_disk #(.CLOCK_HZ(CLOCK_HZ)) dut(
        .mirror_push(1'b0),.mirror_data(8'b0),.mirror_ready(),.dma_lanes(),.dma_reserved(),.clk(clk),.reset(reset),.bus_reset(1'b0),.dma_map_enabled(1'b0),
        .video_reg_read(16'b0),.rk_request(1'b0),.rk_write(1'b0),.rk_address(4'b0),
        .rk_lanes(2'b0),.rk_wdata(16'b0),.rk_irq_ack(1'b0),.cpu_start(cpu_start),
        .io_request(1'b0),.io_address(13'b0),.io_irq_ack(1'b0),
        .sd_request(1'b0),.sd_write(1'b0),.sd_byte(1'b0),.sd_address(2'b0),.sd_wdata(16'b0),
        .sd_miso(1'b1),.dma_ready(1'b0),.dma_error(1'b0),.dma_rdata(16'b0));
    reg [31:0] program_words[0:8];
    initial begin
        repeat(5)@(negedge clk);
        // Load the chosen byte address, including the bit above 16 KiB.
        dut.ram.words[0]=32'(((BOOT_ADDRESS+2048)>>12)<<12)|32'h2b7;
        dut.ram.words[1]=32'(BOOT_ADDRESS<<20)|32'h28293;
        dut.ram.words[2]=32'h00028067;
        // Split 32-bit fetches, followed by a real SW/LW through the
        // serialized cold-data window at 0x4900. The loaded result, rather
        // than the original ALU register, must reach CPU_START.
        program_words[0]=32'h12300513; // ADDI a0,zero,0x123
        program_words[1]=32'h45650513; // ADDI a0,a0,0x456
        program_words[2]=32'h40000337; // LUI t1,0x40000
        program_words[3]=32'h000053b7; // LUI t2,5
        program_words[4]=32'h90038393; // ADDI t2,t2,-0x700
        program_words[5]=32'h00a3a023; // SW a0,0(t2)
        program_words[6]=32'h0003a583; // LW a1,0(t2)
        program_words[7]=32'h30b32223; // SW a1,0x304(t1)
        program_words[8]=32'h0000006f; // J .
        for(integer i=0;i<9;i++)for(integer lane=0;lane<4;lane++)
            dut.ram.words[(BOOT_ADDRESS+4*i+lane)/4][8*((BOOT_ADDRESS+lane)%4)+:8]=program_words[i][8*lane+:8];
        reset=0;
        wait(dut.data_accept && dut.de && dut.da==32'h40000304);
        if(dut.da!==32'h40000304 || dut.dw!==32'h579 || dut.ds!==4'b1111)
            $fatal(1,"tail instruction stream corrupted: addr=%h value=%h",dut.da,dut.dw);
        @(posedge clk);@(negedge clk);
        if(!cpu_start)$fatal(1,"CPU_START not latched");
        $display("PASS SERV tail execution: RV32IC split fetches above 16 KiB and cold SW/LW via the serialized tail");
        $finish;
    end
    initial begin repeat(10000)@(negedge clk);$fatal(1,"SERV tail timeout PC=%h",dut.ia);end
endmodule
