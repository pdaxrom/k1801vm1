`timescale 1ns/1ps
// Warm microbenchmarks on the full native board. The fixture suppresses the
// boot overlay and preloads FRAM; this is not a cold OS boot or hardware test.
module tb_board_bench_cp51;
    reg clk=0,reset=1;
    wire fc,fs,fm,fi,stopped;
    integer workload,k,n,clocks,beats,fetches,writes,requests,busy_clocks;
    integer sck_edges=0,start_sck,start_cs,file_handle;
    reg [15:0] opcode;
    string names[0:8];
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(1'b1),.uart_tx(),.panel_keys(4'b0),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),.boot_complete(),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
    always #5 clk=~clk;
    always @(posedge fs) if(!fc)sck_edges=sck_edges+1;
    task put(input integer address,input [15:0] value);
        begin fram.memory[address]=value[7:0];fram.memory[address+1]=value[15:8];end
    endtask
    task check_retire;
        reg [15:0] expected;
        begin
            // One branch for every 64 retirements. A wrong instruction stream
            // must not silently produce plausible cycle counts.
            if(workload==7)expected=16'o000777;
            else if(n%64==63)expected=16'o000700;
            else case(workload)
                0:expected=16'o010102;
                1:expected=16'o060102;
                2:expected=16'o020102;
                3:case(n%4)
                    0:expected=16'o010102;
                    1:expected=16'o060102;
                    2:expected=16'o030102;
                    3:expected=16'o160102;
                  endcase
                4:expected=16'o011302;
                5:expected=16'o010114;
                6:expected=16'o011314;
                8:expected=n%2==0 ? 16'o010146 : 16'o012602;
                default:expected=0;
            endcase
            if(dut.cpu.ir!==expected)$fatal(1,"benchmark stream workload%0d retirement%0d got%o expected%o",workload,n,dut.cpu.ir,expected);
        end
    endtask
    initial begin
        names[0]="MOV_RR";names[1]="ADD_RR";names[2]="CMP_RR";
        names[3]="mixed_RR";names[4]="MOV_mem_reg";names[5]="MOV_reg_mem";
        names[6]="MOV_mem_mem";names[7]="BR_self";names[8]="stack_push_pop";
        file_handle=$fopen("build/cp51-board-bench.json","w");
        if(file_handle==0)$fatal(1,"benchmark output open failed");
        $fwrite(file_handle,"[\n");
        force dut.bus.boot_overlay_active=1'b0;
        for(workload=0;workload<9;workload=workload+1)begin
            @(negedge clk);reset=1;
            repeat(5)@(negedge clk);
            // Guest instructions establish registers; no RF/PSW injection.
            put(0,16'o012701);put(2,16'o000003); // MOV #3,R1
            put(4,16'o012702);put(6,16'o000005); // MOV #5,R2
            put(8,16'o012703);put(10,16'o010000);
            put(12,16'o012704);put(14,16'o010002);
            put(16,16'o012706);put(18,16'o014000);
            put(20,16'o000137);put(22,16'o001000); // JMP @#loop
            put(4096,16'h1357);put(4098,16'h2468);
            for(k=0;k<64;k=k+1)begin
                case(workload)
                    0:opcode=16'o010102;
                    1:opcode=16'o060102;
                    2:opcode=16'o020102;
                    3:case(k%4)
                        0:opcode=16'o010102;
                        1:opcode=16'o060102;
                        2:opcode=16'o030102;
                        3:opcode=16'o160102;
                      endcase
                    4:opcode=16'o011302;
                    5:opcode=16'o010114;
                    6:opcode=16'o011314;
                    7:opcode=16'o000777;
                    8:opcode=k%2==0 ? 16'o010146 : 16'o012602;
                    default:opcode=0;
                endcase
                if(k==63 && workload!=7)opcode=16'o000700;
                put(512+2*k,opcode);
            end
            reset=0;
            n=0;clocks=0;
            // Six setup instructions, then one complete warm loop.
            while(n<70)begin
                @(posedge clk);#1;clocks=clocks+1;
                if(stopped || clocks>100000)$fatal(1,"benchmark warmup failed");
                if(dut.cpu.retire)n=n+1;
            end
            n=0;clocks=0;beats=0;fetches=0;writes=0;requests=0;busy_clocks=0;
            start_sck=sck_edges;start_cs=fram.transaction_count;
            while(n<256)begin
                @(posedge clk);
                if(dut.request)requests=requests+1;
                if(dut.bus.fram_busy)busy_clocks=busy_clocks+1;
                if(dut.request && dut.acknowledge)begin
                    beats=beats+1;
                    if(dut.opcode_fetch)fetches=fetches+1;
                    if(dut.writing)writes=writes+1;
                end
                #1;clocks=clocks+1;
                if(stopped || dut.error || clocks>200000)$fatal(1,"benchmark stopped/faulted");
                if(dut.cpu.retire)begin check_retire;n=n+1;end
            end
            if(fetches!=256)$fatal(1,"benchmark fetch count %0d",fetches);
            if(workload<=3 || workload==7)begin
                if(beats!=256 || writes!=0 || sck_edges-start_sck!=12288)
                    $fatal(1,"register workload memory counts");
            end
            if(workload==4 && dut.cpu.engine.dp.rf.words[2]!==16'h1357)$fatal(1,"memory read result");
            if(workload==5 && {fram.memory[4099],fram.memory[4098]}!==16'd3)$fatal(1,"memory write result");
            if(workload==6 && {fram.memory[4099],fram.memory[4098]}!==16'h1357)$fatal(1,"memory copy result");
            $fwrite(file_handle,"{\"workload\":\"%s\",\"instructions\":256,\"microclocks\":%0d,\"memory_beats\":%0d,\"opcode_fetches\":%0d,\"writes\":%0d,\"request_clocks\":%0d,\"fram_busy_clocks\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                names[workload],clocks,beats,fetches,writes,requests,busy_clocks,
                fram.transaction_count-start_cs,sck_edges-start_sck,workload==8 ? "" : ",");
            $display("PASS CP51 %s: %0d clocks / 256 instructions, %0d beats, %0d SCK, %0d busy clocks",
                names[workload],clocks,beats,sck_edges-start_sck,busy_clocks);
        end
        $fwrite(file_handle,"]\n");$fclose(file_handle);$finish;
    end
    initial begin #20000000;$fatal(1,"benchmark timeout");end
endmodule
