`timescale 1ns/1ps
// Same functional/reset scenarios as tb_sram, with 50 MHz and the
// conservative maximum FPGA pin delays used by the board timing contract.
module tb_sram50 #(parameter integer PIN_DELAY=10,DQ_DELAY=15,FAST_RESPONSE=0);
    reg clk=0, power_on=1, reset=1, request=0, writing=0;
    reg [19:0] address=0;
    reg [1:0] lanes=3;
    reg [15:0] data=0;
    wire [15:0] result,dq;
    wire [19:0] a;
    wire ready,initialized,ce,oe,we,lb,ub;
    integer i,before_writes,checks=0;
    always #10 clk=~clk;
    uj11_sram #(.CLEAR_WORDS(16),.FAST_RESPONSE(FAST_RESPONSE)) dut(clk,power_on,reset,request,writing,address,
        lanes,data,result,ready,initialized,a,dq,ce,oe,we,lb,ub);
    wire [19:0] board_address;
    wire [15:0] board_data;
    wire board_ce,board_oe,board_we,board_lb,board_ub;
    // 10 ns address/control output, 15 ns DQ/enable output, 1 ns PCB each way.
    assign #(PIN_DELAY+1) board_address=a;
    assign #(PIN_DELAY+1) {board_ce,board_oe,board_we,board_lb,board_ub}={ce,oe,we,lb,ub};
    assign #(DQ_DELAY+1) board_data=dut.drive_data ? dut.data_out : 16'bz;
    assign #1 dq=!dut.drive_data ? board_data : 16'bz;
    async_sram_model ram(board_address,board_data,board_ce,board_oe,board_we,board_lb,board_ub);
    task exchange(input bit wr,input [19:0] addr,input [1:0] be,input [15:0] value);
        begin
            @(negedge clk); request=1;writing=wr;address=addr;lanes=be;data=value;
            // A synchronous client consumes ready on the following rising edge.
            wait(ready); @(posedge clk); @(negedge clk);
            before_writes=ram.writes;
            repeat(5)@(negedge clk);
            if(ram.writes!=before_writes)$fatal(1,"held request repeated write");
            request=0; repeat(2)@(negedge clk);
        end
    endtask
    task expect_word(input [19:0] addr,input [15:0] want);
        begin exchange(0,addr,3,0); if(result!==want)$fatal(1,"word %h got %h want %h",addr,result,want); checks=checks+1; end
    endtask
    initial begin
        for(i=0;i<32;i=i+1)ram.memory[i]=8'ha5;
        repeat(4)@(negedge clk);power_on=0;
        wait(initialized);@(negedge clk);reset=0;
        for(i=0;i<16;i=i+1)expect_word(i,0);
        exchange(1,20'h00000,3,16'habcd);exchange(1,20'h08000,3,16'h1234);
        exchange(1,20'h0ffff,3,16'h7654);exchange(1,20'hfffff,3,16'hbeef);
        expect_word(0,16'habcd);expect_word(20'h08000,16'h1234);
        expect_word(20'h0ffff,16'h7654);expect_word(20'hfffff,16'hbeef);
        exchange(1,0,1,16'h0099);expect_word(0,16'hab99);
        exchange(1,0,2,16'h5500);expect_word(0,16'h5599);
        @(negedge clk);reset=1;repeat(4)@(negedge clk);reset=0;
        expect_word(0,16'h5599);expect_word(20'h08000,16'h1234);
        // Abort a read; a subsequent request must complete normally.
        @(negedge clk);request=1;writing=0;repeat(2)@(negedge clk);
        reset=1;request=0;repeat(3)@(negedge clk);reset=0;
        expect_word(0,16'h5599);
        // Reset during each active write phase must preserve the full SRAM
        // pulse/hold and must never acknowledge that write as a new request.
        for(i=1;i<=5;i=i+1) begin
            @(negedge clk);request=1;writing=1;address=1;lanes=3;data=16'hd000+i;
            repeat(i)@(negedge clk);
            reset=1;request=0;@(negedge clk);reset=0;
            repeat(8) begin @(negedge clk);if(ready)$fatal(1,"cancelled write acknowledged");end
            expect_word(1,16'hd000+i);
            expect_word(0,16'h5599);
        end
        @(negedge clk);power_on=1;repeat(3)@(negedge clk);power_on=0;
        wait(initialized);expect_word(0,0);expect_word(20'h08000,16'h1234);
        $display("PASS SRAM 50 MHz with pin/PCB delays: %0d checks, byte lanes, banks, 2MiB boundary, retention, power-on and handshake",checks);$finish;
    end
    initial begin #1000000;$fatal(1,"SRAM timeout");end
endmodule
