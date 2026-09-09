`timescale 1ns/1ps
// CPU + SPI FRAM + actual frozen KL11/KW11 bus. Configuration writes are
// testbench bus cycles; all CPU instructions, vectors and frames use FRAM.
module tb_irq_lsi11;
    reg clk=0,reset_all=1,hold_core=1,manual=0,manual_request=0,uart_rx=1;
    reg [15:0] manual_address=0,manual_data=0;
    wire reset=reset_all || hold_core;
    wire [15:0] io_address,io_wdata,io_rdata,legacy_data,vector;
    wire io_request,io_write,io_byte,io_ack,cs_n,sck,mosi,miso;
    wire uart_tx,virq,event_irq,sd_cs_n,sd_sck,sd_mosi,legacy_cs,legacy_sck;
    wire stopped,retire,rf_write,memory_request,memory_write,memory_ack;
    wire [15:0] ir,mdr,psw,q,rf_data;wire [1:0] fault;wire [9:0] upc;wire [35:0] uword;wire [3:0] rf_address;
    wire irq_valid,irq_ack,uart_ack,timer_pending,waiting;wire [2:0] irq_priority;wire [8:1] irq_vector;
    wire [1:0] lanes=manual ? 2'b11 : io_byte ? (io_address[0] ? 2'b10 : 2'b01) : 2'b11;
    wire [15:0] lane_data=manual ? manual_data : io_byte && io_address[0] ? {io_wdata[7:0],8'b0} : io_wdata;
    assign io_rdata=io_byte ? (io_address[0] ? {8'b0,legacy_data[15:8]} : {8'b0,legacy_data[7:0]}) : legacy_data;
    wire stream=uword[35] && uword[34:31]==4'd2;
    integer c,k,scenario,ack_count=0,retire_count=0,wait_retire=0,cycles=0,checks=0,old_retire,old_spi;
    reg [15:0] accepted_vectors[0:7];
    reg pending=0;reg [33:0] held;
    always #5 clk=~clk;
    uj11_irq_lsi11 irq(.clk(clk),.reset(reset_all),.event_irq(event_irq),.uart_irq(virq),
        .uart_vector(vector[8:1]),.irq_ack(irq_ack),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.uart_ack(uart_ack),.timer_pending(timer_pending));
    uj11_fram_system system(.clk(clk),.reset(reset),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.waiting(waiting),.peripheral_reset(),
        .spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso),
        .io_request(io_request),.io_write(io_write),.io_byte(io_byte),.io_address(io_address),.io_wdata(io_wdata),
        .io_rdata(io_rdata),.io_ack(io_ack && !manual),.io_error(1'b0),.stopped(stopped),.retire(retire),
        .fault_code(fault),.debug_upc(upc),.debug_uword(uword),.ir(ir),.mdr(mdr),.psw(psw),.q(q),
        .debug_rf_write(rf_write),.debug_rf_address(rf_address),.debug_rf_data(rf_data),
        .memory_request(memory_request),.memory_write(memory_write),.memory_ack(memory_ack));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    am4_hc1200_cpu11_bus #(.CLOCK_HZ(1843200),.TICK_DIVISOR(100000),
        .BOOT_ROM_ENABLE(0),.SD_BOOT_ENABLE(1),.RK_SERVICE_ENABLE(0),.SD_SLOW_DIV(2),.SD_FAST_DIV(1)) legacy(
        .clk(clk),.rst(reset_all),.peripheral_reset(1'b0),.request(manual ? manual_request : io_request),.write(manual ? 1'b1 : io_write),
        .byte_select(lanes),.address(manual ? manual_address : io_address),.wdata(lane_data),.instruction_fetch(stream),
        .rdata(legacy_data),.acknowledge(io_ack),.virq(virq),.interrupt_vector(vector),
        .interrupt_strobe(uart_ack),.interrupt_acknowledge(),.event_irq(event_irq),.uart_rx(uart_rx),.uart_tx(uart_tx),
        .panel_key_rows(4'ha),.panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_reg_latch(),
        .spi_cs_n(legacy_cs),.spi_sck(legacy_sck),.spi_mosi(),.spi_miso(1'b0),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(1'b1),
        .boot_rom_ena(),.boot_rom_addr(),.boot_rom_data(8'b0),.boot_complete(),.host_miso(),.host_miso_oe());
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    always @(posedge clk)begin
        if(reset)begin ack_count=0;retire_count=0;wait_retire=0;cycles=0;pending=0;end
        else begin
            cycles=cycles+1;
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
        if(legacy_sck || !legacy_cs)$fatal(1,"legacy IRQ test used private legacy FRAM");
    end
    task tick;begin @(posedge clk);#1;end endtask
    task poke;input [15:0] a,v;begin fram.memory[a]=v[7:0];fram.memory[a+16'd1]=v[15:8];end endtask
    task csr;input [15:0] a,v;begin
        @(negedge clk);manual=1;manual_request=1;manual_address=a;manual_data=v;
        c=0;begin : write_wait
            forever begin @(posedge clk);if(io_ack)begin #1;disable write_wait;end
                c=c+1;if(c>30)$fatal(1,"manual CSR timeout");end
        end
        @(negedge clk);manual_request=0;tick;@(negedge clk);manual=0;
    end endtask
    task send_uart;input [7:0] v;integer b;begin
        @(negedge clk);uart_rx=0;repeat(16)tick;
        for(b=0;b<8;b=b+1)begin @(negedge clk);uart_rx=v[b];repeat(16)tick;end
        @(negedge clk);uart_rx=1;repeat(24)tick;
    end endtask
    task wait_idle;input integer required_irqs;begin
        c=0;while((!waiting || ack_count!=required_irqs || timer_pending || virq) && c<15000)begin tick;c=c+1;end
        if(c==15000)$fatal(1,"IRQ idle timeout scenario%0d ack%0d waiting%b pc%h",scenario,ack_count,waiting,system.core.engine.dp.rf.words[7]);
        repeat(200)tick;old_retire=retire_count;old_spi=fram.transaction_count;
        repeat(200)tick;
        if(retire_count!=old_retire || fram.transaction_count!=old_spi || irq_ack || memory_request)
            $fatal(1,"WAIT repeated retirement or bus work");
        if(system.core.engine.dp.rf.words[6]!==16'h6000 || system.core.engine.dp.rf.words[7]!==16'd6)
            $fatal(1,"IRQ frame/RTI SP/PC");
        checks=checks+1;
    end endtask
    initial begin
        #100;
        for(scenario=0;scenario<5;scenario=scenario+1)begin
            @(negedge clk);reset_all=1;hold_core=1;tick;@(negedge clk);reset_all=0;
            poke(0,scenario==4 ? 16'o000001 : 16'o000230); // masked WAIT or SPL0
            poke(2,scenario==2 ? 16'o010524 : 16'o012405); // write/read FRAM operand
            poke(4,16'o000001);poke(6,16'o000776); // WAIT; BR WAIT
            poke(16'o100,16'h100);poke(16'o102,16'o340);
            poke(16'o60,16'h120);poke(16'o62,16'o340);
            poke(16'o64,16'h140);poke(16'o66,16'o340);
            poke(16'h100,16'o005200);poke(16'h102,16'o000002); // INC R0; RTI
            poke(16'h120,16'o005201);poke(16'h122,16'o113703); // INC R1; MOVB @#RBUF,R3; RTI
            poke(16'h124,16'o177562);poke(16'h126,16'o000002);
            poke(16'h140,16'o005202);poke(16'h142,16'o000002);
            poke(16'h4000,16'h1234);
            if(scenario==0)begin
                csr(16'o177560,16'o100);send_uart(8'ha7);csr(16'o177564,16'o100);
                csr(16'o177546,16'o300); // real KW11 one-clock event on DONE+IE
                if(!virq || !timer_pending)$fatal(1,"actual peripheral requests absent");
            end
            @(negedge clk);hold_core=0;repeat(17)tick;@(negedge clk);
            system.core.engine.dp.rf.words[6]=16'h6000;
            system.core.engine.dp.rf.words[4]=16'h4000;system.core.engine.dp.rf.words[5]=16'ha55a;
            if(scenario==1 || scenario==2)begin
                c=0;while(!(memory_request && system.address==16'h4000 && !memory_ack) && c<1000)begin tick;c=c+1;end
                if(c==1000)$fatal(1,"did not reach FRAM operand");
                csr(16'o177546,16'o300);
                if(!timer_pending || ack_count!=0)$fatal(1,"timer pulse lost or IRQ cancelled FRAM beat");
            end
            if(scenario==4)begin
                while(!waiting)tick;
                csr(16'o177546,16'o300);repeat(500)tick;
                if(!timer_pending || ack_count || !waiting || wait_retire!=1 || psw!==16'o340)
                    $fatal(1,"IPL7 WAIT masking");
                @(negedge clk);reset_all=1;tick;
                if(timer_pending || waiting || irq_ack)$fatal(1,"reset did not clear WAIT/pending");
                checks=checks+1;
            end else begin
                wait_idle(scenario==0 ? 3 : scenario==3 ? 0 : 1);
                if(scenario==0)begin
                    if(accepted_vectors[0]!==16'o100 || accepted_vectors[1]!==16'o60 || accepted_vectors[2]!==16'o64 ||
                       system.core.engine.dp.rf.words[1]!==1 || system.core.engine.dp.rf.words[2]!==1 ||
                       system.core.engine.dp.rf.words[3]!==16'hffa7)$fatal(1,"UART/timer arbitration or RX data");
                end
                if(scenario==2 && {fram.memory[16'h4001],fram.memory[16'h4000]}!==16'ha55a)$fatal(1,"IRQ lost FRAM store");
                if(scenario==1 || scenario==0)if(system.core.engine.dp.rf.words[5]!==16'h1234)$fatal(1,"IRQ lost FRAM read");
                if(scenario==3)begin csr(16'o177546,16'o300);wait_idle(1);if(wait_retire!=2)$fatal(1,"WAIT wake retirement");end
                if(system.core.engine.dp.rf.words[0]!==1)$fatal(1,"timer handler missing/duplicated");
            end
        end
        $display("PASS legacy IRQ: 5 scenarios, %0d settled-state checks; actual KL11 RX/TX + KW11, FRAM read/write retention, WAIT masking/wakeup/reset",checks);$finish;
    end
    initial begin #2000000;$fatal(1,"legacy IRQ global timeout");end
endmodule
