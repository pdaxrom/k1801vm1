`timescale 1ns/1ps
// Public CSR transactions plus emulated private-firmware completion.
module tb_rk_recovery_cp60;
    reg clk=0,reset=1,request=0,write=0,fetch=0,irq_ack=0;
    reg [15:0] address=0,data=0;
    reg [1:0] lanes=3;
    wire [15:0] value,vector,rom_data;
    wire ack,irq,rom_enable;
    wire [2:0] priority_level;
    wire [8:0] rom_address;
    wire [1:0] rom_write;
    integer guard,checks=0;
    reg [15:0] answer;
    always #5 clk=~clk;
    uj11_board_bus #(.SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(1)) dut(
        .clk(clk),.rst(reset),.peripheral_reset(1'b0),.request(request),.write(write),
        .byte_select(lanes),.bank(1'b0),.physical(1'b0),.address(address),.wdata(data),
        .instruction_fetch(fetch),.rdata(value),.acknowledge(ack),.virq(irq),
        .interrupt_vector(vector),.interrupt_priority(priority_level),
        .interrupt_strobe(irq_ack),.interrupt_acknowledge(),.event_irq(),
        .uart_rx(1'b1),.uart_tx(),.panel_key_rows(4'b0),.panel_din(),.panel_ce(),
        .panel_clk(),.panel_rs(),.panel_blank(),.panel_reg_latch(),.host_miso(),.host_miso_oe(),
        .spi_cs_n(),.spi_sck(),.spi_mosi(),.spi_miso(1'b1),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),
        .boot_rom_ena(rom_enable),.boot_rom_addr(rom_address),.boot_rom_write(rom_write),
        .boot_rom_data(rom_data),.boot_complete());
    uj11_firmware_rom rom(clk,rom_enable,rom_address,rom_data,rom_write,data);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task beat(input bit wr,input [1:0] mask,input [15:0] addr,datum,input bit opcode);
        begin
            @(negedge clk);request=1;write=wr;lanes=mask;address=addr;data=datum;fetch=opcode;
            guard=0;@(posedge clk);
            while(!ack)begin @(posedge clk);guard=guard+1;if(guard>100)$fatal(1,"CSR timeout");end
            answer=value;checks=checks+1;
            @(negedge clk);request=0;repeat(3)@(negedge clk);
        end
    endtask
    task accept;
        begin @(negedge clk);irq_ack=1;repeat(2)@(negedge clk);irq_ack=0;repeat(3)@(negedge clk);end
    endtask
    task start_service(input [15:0] command);
        begin
            beat(1,3,16'o177440,command,0);
            if(!irq || vector!=16'o160000 || priority_level!=7)$fatal(1,"private command dispatch %o",command);
            accept();if(!dut.rk_service_active || irq)$fatal(1,"private entry");
        end
    endtask
    task complete_service(input [15:0] status,input bit want_irq);
        begin
            beat(1,3,16'o177440,status,0);
            beat(0,3,16'o177440,0,0);
            if(answer!==status || irq || !dut.rk_service_active)$fatal(1,"private CERR mistaken for CCLR");
            beat(0,3,16'o160476,0,1);
            if(dut.rk_service_active || irq!==want_irq)$fatal(1,"private completion IRQ count");
            if(want_irq)begin
                if(vector!=16'o210 || priority_level!=5)$fatal(1,"guest completion vector");
                accept();if(irq)$fatal(1,"repeated completion IRQ");
            end
        end
    endtask
    task check_clear;
        begin
            if(irq || dut.rk_service_pending || dut.rk_interrupt_enable || dut.rk_cs1_initialized)
                $fatal(1,"CCLR generated stale IRQ/pending command");
            beat(0,3,16'o177440,0,0);
            if(answer!==16'o200)$fatal(1,"CCLR not READY");
        end
    endtask
    initial begin
        repeat(8)@(negedge clk);reset=0;
        start_service(16'o002113);complete_service(16'o042312,1);
        beat(1,3,16'o177440,16'o142312,0);check_clear();
        beat(1,3,16'o177440,16'o100121,0);check_clear(); // CCLR wins over GO/IE.
        start_service(16'o002121);complete_service(16'o102320,1);
        beat(1,3,16'o177440,16'o102320,0);check_clear(); // Exact RT-11 error-handler write.
        beat(1,3,16'o177440,16'o100000,0);check_clear(); // Repeated clear is quiet.
        start_service(16'o002013);complete_service(16'o042212,0);
        beat(1,2,16'o177441,16'o100000,0);check_clear();
        start_service(16'o002123);complete_service(16'o002322,1);
        beat(1,3,16'o177450,16'o40,0);check_clear(); // Existing SCLR preserved.
        $display("PASS CP60 RK recovery: %0d CSR beats; RECAL/READ/WRITE, CERR, CCLR/SCLR, exact IRQ completion",checks);
        $finish;
    end
    initial begin #1000000;$fatal(1,"RK recovery timeout");end
endmodule
