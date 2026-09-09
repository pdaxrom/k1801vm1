`timescale 1ns/1ps
// CPU + SPI FRAM + actual frozen KL11/KW11 bus. Configuration writes are
// testbench bus cycles; all CPU instructions, vectors and frames use FRAM.
module tb_system_control_lsi11;
    reg clk=0,reset_all=1,hold_core=1,manual=0,manual_request=0,uart_rx=1;
    reg [15:0] manual_address=0,manual_data=0;
    wire reset=reset_all || hold_core;
    wire [15:0] io_address,io_wdata,io_rdata,legacy_data,vector;
    wire io_request,io_write,io_byte,io_ack,cs_n,sck,mosi,miso;
    wire uart_tx,virq,event_irq,sd_cs_n,sd_sck,sd_mosi,legacy_cs,legacy_sck,legacy_mosi,legacy_miso;
    wire stopped,retire,rf_write,memory_request,memory_write,memory_ack;
    wire [15:0] ir,mdr,psw,q,rf_data;wire [1:0] fault;wire [9:0] upc;wire [35:0] uword;wire [3:0] rf_address;
    wire peripheral_reset; integer resets=0;
    wire irq_valid,irq_ack,uart_ack,timer_pending,waiting;wire [2:0] irq_priority;wire [8:1] irq_vector;
    wire [1:0] lanes=manual ? 2'b11 : io_byte ? (io_address[0] ? 2'b10 : 2'b01) : 2'b11;
    wire [15:0] lane_data=manual ? manual_data : io_byte && io_address[0] ? {io_wdata[7:0],8'b0} : io_wdata;
    assign io_rdata=io_byte ? (io_address[0] ? {8'b0,legacy_data[15:8]} : {8'b0,legacy_data[7:0]}) : legacy_data;
    wire stream=uword[35] && uword[34:31]==4'd2;
    integer c,k,scenario,ack_count=0,retire_count=0,wait_retire=0,cycles=0,checks=0,old_retire,old_spi;
    reg [15:0] accepted_vectors[0:7];
    reg pending=0;reg [33:0] held;
    always #5 clk=~clk;
    uj11_irq_lsi11 irq(.clk(clk),.reset(reset_all || peripheral_reset),.event_irq(event_irq),.uart_irq(virq),
        .uart_vector(vector[8:1]),.irq_ack(irq_ack),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.uart_ack(uart_ack),.timer_pending(timer_pending));
    uj11_fram_system system(.clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(peripheral_reset),
        .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso),
        .io_request(io_request),.io_write(io_write),.io_byte(io_byte),.io_address(io_address),.io_wdata(io_wdata),
        .io_rdata(io_rdata),.io_ack(io_ack && !manual),.io_error(1'b0),.stopped(stopped),.retire(retire),
        .fault_code(fault),.debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data),
        .memory_request(memory_request),.memory_write(memory_write),.memory_ack(memory_ack));
    spi_fram_model peripheral_bank(.cs_n(legacy_cs),.sck(legacy_sck),.mosi(legacy_mosi),.miso(legacy_miso));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    am4_hc1200_cpu11_bus #(.CLOCK_HZ(1843200),.TICK_DIVISOR(100000),
        .BOOT_ROM_ENABLE(0),.SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(1),.SD_SLOW_DIV(2),.SD_FAST_DIV(1)) legacy(
        .clk(clk),.rst(reset_all),.peripheral_reset(peripheral_reset),.request(manual ? manual_request : io_request),.write(manual ? 1'b1 : io_write),
        .byte_select(lanes),.address(manual ? manual_address : io_address),.wdata(lane_data),.instruction_fetch(stream),
        .rdata(legacy_data),.acknowledge(io_ack),.virq(virq),.interrupt_vector(vector),
        .interrupt_strobe(uart_ack),.interrupt_acknowledge(),.event_irq(event_irq),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .panel_key_rows(4'ha),.panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_reg_latch(),
        .spi_cs_n(legacy_cs),.spi_sck(legacy_sck),.spi_mosi(legacy_mosi),.spi_miso(legacy_miso),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(1'b1),
        .boot_rom_ena(),.boot_rom_addr(),.boot_rom_data(8'b0),.boot_complete(),.host_miso(),.host_miso_oe());
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    always @(posedge clk)begin
        if(reset)begin ack_count=0;retire_count=0;wait_retire=0;cycles=0;pending=0;resets=0;end
        else begin
            cycles=cycles+1;
            if(peripheral_reset)begin
                if(upc!==10'h022 || memory_request || irq_ack || rf_write || psw!==16'he9 ||
                   system.core.engine.dp.rf.words[7]!==16'd6 || !timer_pending)
                    $fatal(1,"RESET pulse boundary/CPU state");
                resets=resets+1;
            end
            if(stopped)$fatal(1,"legacy IRQ core stopped, scenario%0d fault%0d upc%h",scenario,fault,upc);
            if(retire)begin retire_count=retire_count+1;if(ir==1)wait_retire=wait_retire+1;end
            if(irq_ack)begin
                if(ack_count>=8 || (memory_request && !memory_ack))$fatal(1,"IRQ acknowledged during uncompleted transfer");
                accepted_vectors[ack_count]={7'b0,irq_vector,1'b0};ack_count=ack_count+1;
            end
            if(pending && {memory_write,system.byte_access,system.address,system.wdata}!==held)
                $fatal(1,"pending FRAM transaction changed during IRQ");
            if(memory_request && !memory_ack)begin
                pending=1;held={memory_write,system.byte_access,system.address,system.wdata};
            end else pending=0;
        end
        if((legacy_sck || !legacy_cs) && !(manual && manual_address>=16'o177440 && manual_address<=16'o177476))$fatal(1,"CPU memory reached private peripheral FRAM");
    end
    task tick;begin @(posedge clk);#1;end endtask
    task poke;input [15:0] a,v;begin fram.memory[a]=v[7:0];fram.memory[a+16'd1]=v[15:8];end endtask
    task csr;input [15:0] a,v;begin
        @(negedge clk);manual=1;manual_request=1;manual_address=a;manual_data=v;
        c=0;begin : write_wait
            forever begin @(posedge clk);if(io_ack)begin #1;disable write_wait;end
                c=c+1;if(c>400)$fatal(1,"manual CSR timeout");end
        end
        @(negedge clk);manual_request=0;tick;@(negedge clk);manual=0;
    end endtask
    task send_uart;input [7:0] v;integer b;begin
        @(negedge clk);uart_rx=0;repeat(16)tick;
        for(b=0;b<8;b=b+1)begin @(negedge clk);uart_rx=v[b];repeat(16)tick;end
        @(negedge clk);uart_rx=1;repeat(24)tick;
    end endtask
    initial begin
        #100;
        for(scenario=0;scenario<3;scenario=scenario+1)begin
            @(negedge clk);reset_all=1;hold_core=1;tick;@(negedge clk);reset_all=0;
            poke(0,16'o010514);poke(2,16'o011403); // MOV R5,(R4); MOV (R4),R3
            poke(4,16'o000005);poke(6,16'o000230); // RESET; SPL0
            poke(8,16'o000001);poke(10,16'o000776); // WAIT; BR WAIT
            poke(16'o100,16'h100);poke(16'o102,16'o340);
            poke(16'h100,16'o005200);poke(16'h102,16'o000002);
            poke(16'h4000,16'h1234);poke(16'h4002,16'hbeef);
            csr(16'o166000,16'hff00);csr(16'o177502,16'd2);
            csr(16'o177440,16'o100); // RK interrupt enable, no command
            csr(16'o177560,16'o100);send_uart(8'ha7);csr(16'o177564,16'o100);
            if(scenario==0)csr(16'o177546,16'o300);
            if(!virq || sd_cs_n || legacy.panel_output!==8'hff)$fatal(1,"peripheral setup absent");
            @(negedge clk);hold_core=0;repeat(17)tick;@(negedge clk);
            system.core.engine.dp.rf.words[6]=16'h6000;
            system.core.engine.dp.rf.words[4]=16'h4000;system.core.engine.dp.rf.words[5]=16'ha55a;
            system.core.engine.status.psw=16'hef;system.core.engine.dp.q=16'hbabe;
            if(scenario>0)begin
                c=0;while(!(memory_request && system.address==16'h4000 && memory_write==(scenario==2) && !memory_ack) && c<2000)begin tick;c=c+1;end
                if(c==2000)$fatal(1,"did not reach FRAM operand");
                csr(16'o177546,16'o300);
                if(!timer_pending || ack_count!=0)$fatal(1,"timer pulse lost during FRAM beat");
            end
            c=0;while(!waiting && c<10000)begin tick;c=c+1;end
            repeat(200)tick;
            if(c==10000 || resets!=1 || q!==16'hbabe || ack_count || timer_pending || virq || !waiting || psw!==16'h9 ||
               system.core.engine.dp.rf.words[7]!==16'd10 || system.core.engine.dp.rf.words[6]!==16'h6000 ||
               system.core.engine.dp.rf.words[3]!==16'ha55a || system.core.engine.dp.rf.words[4]!==16'h4000 ||
               system.core.engine.dp.rf.words[5]!==16'ha55a ||
               {fram.memory[16'h4001],fram.memory[16'h4000]}!==16'ha55a ||
               {fram.memory[16'h4003],fram.memory[16'h4002]}!==16'hbeef)
                $fatal(1,"RESET failed CPU/FRAM/IRQ preservation scenario%0d reset%0d irq%0d psw%h",scenario,resets,ack_count,psw);
            if(legacy.panel_output!==8'h12 || !legacy.timer_done || legacy.timer_ie ||
               !sd_cs_n || sd_sck || legacy.rk_interrupt_enable || legacy.rk_service_pending || legacy.rk_service_active || legacy.fixed_uart.console.rx_full || legacy.fixed_uart.console.tx_busy)
                $fatal(1,"RESET failed panel/KW11/SD/service reset");
            old_retire=retire_count;old_spi=fram.transaction_count;repeat(200)tick;
            if(retire_count!=old_retire || fram.transaction_count!=old_spi || memory_request)$fatal(1,"RESET stale interrupt/prefetch");
            csr(16'o177546,16'o300);
            c=0;while((ack_count!=1 || !waiting || wait_retire!=2) && c<10000)begin tick;c=c+1;end
            repeat(200)tick;
            if(c==10000 || resets!=1 || ack_count!=1 || timer_pending || virq || accepted_vectors[0]!==16'o100 ||
               system.core.engine.dp.rf.words[0]!==1 || system.core.engine.dp.rf.words[6]!==16'h6000 ||
               system.core.engine.dp.rf.words[7]!==16'd10 || psw!==16'h9)
                $fatal(1,"post-RESET timer/WAIT/RTI not reusable");
            checks=checks+1;
        end
        $display("PASS system control legacy: %0d scenarios; actual KL11/KW11/panel/SD/RK reset, FRAM retention, IRQ clear and new WAIT wake",checks);$finish;
    end
    initial begin #2000000;$fatal(1,"system control legacy timeout");end
endmodule
