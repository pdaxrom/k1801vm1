`timescale 1ns/1ps
module tb_storage_rk_rq;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,power_on=1,peripheral_reset=0;
    always #20.833 clk=~clk;
    reg request=0,writing=0,byte_access=0;
    reg [21:0] address=0;reg [15:0] write_data=0;
    wire ready,error,initialized,boot_complete,irq_valid;
    wire [15:0] read_data,irq_vector;
    wire [2:0] irq_priority;
    reg irq_ack=0,dma_map_enabled=0;
    wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    wire tx,cs,sck,mosi,miso;wire [7:0] pins;
    integer checks=0,n;
    uj11_mmu_board_bus #(.BOOT_ROM_ENABLE(0),.CLEAR_WORDS(1),.TICK_DIVISOR(1024),.CLOCK_HZ(240000),.SD_SLOW_DIV(4),.SD_FAST_DIV(2)) dut(
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),.cpu_lock(1'b0),.dma_map_enabled(dma_map_enabled),
        .request(request),.writing(writing),.byte_access(byte_access),.address(address),.write_data(write_data),
        .ready(ready),.error(error),.read_data(read_data),.irq_valid(irq_valid),.irq_priority(irq_priority),
        .irq_vector(irq_vector),.irq_ack(irq_ack),.uart_rx(1'b1),.uart_tx(tx),
        .panel_keys(4'b0),.panel_pins(pins),.memory_initialized(initialized),
        .sram_address(sa),.sram_data(sd),.sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),.boot_complete(boot_complete));
    serv_memory_guard guard(.clk(clk),.reset(dut.disk.iop_reset),
        .write(dut.disk.data_accept && dut.disk.memory_selected && dut.disk.de),.address(dut.disk.da));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s address=%h data=%h",why,address,read_data);checks++;end
    endtask
    localparam RK=22'o17777400,RQ=22'o17772150,COMM=22'h12000;
    localparam CMD=22'h13004,RSP=22'h13104,DATA=22'h180000;
    integer ring_index=0;
    reg [31:0] trace_pc[0:127],trace_instruction[0:127];integer trace_index=0;
    always @(posedge clk)if(!reset)begin
        if(dut.disk.ic && dut.disk.ia_ack)begin
            trace_pc[trace_index%128]<=dut.disk.ia;trace_instruction[trace_index%128]<=dut.disk.mem_data;trace_index<=trace_index+1;
        end
        if(dut.disk.ic && dut.disk.ia>=12288)begin
            for(integer t=0;t<128;t++)$display("SERV %h %h",trace_pc[(trace_index+t)%128],trace_instruction[(trace_index+t)%128]);
            $fatal(1,"SERV fetch outside RAM: %h",dut.disk.ia);
        end
    end
    function [15:0] word_at(input integer a);word_at={ram.memory[a+1],ram.memory[a]};endfunction
    function [7:0] pattern(input integer lba,input integer offset);pattern=8'((lba*17)^(offset*3)^(offset>>8));endfunction
    task put(input [21:0] a,input [15:0] v);bus(1,0,a,v,0);endtask
    task expect_word(input [21:0] a,input [15:0] v);
        bus(0,0,a,0,0);check(value===v,$sformatf("word %h got %h want %h",a,value,v));
    endtask
    task acknowledge;
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;
    endtask
    task rk_done(input [15:0] er);
        integer polls;
        polls=0;
        do begin bus(0,0,RK+4,0,0);polls++;check(polls<100000,"RK command timeout");end while(!value[7]);
        expect_word(RK+2,er);
    endtask
    task rk_io(input [15:0] fn,input integer count,input integer addr,input [15:0] da);
        put(RK+6,-count);put(RK+8,addr);put(RK+10,da);put(RK+4,fn|((addr>>12)&16'o60));
    endtask
    localparam UBM=22'o17770200;
    task map_page(input integer page,input [21:0] base);
        put(UBM+4*page,base);put(UBM+4*page+2,base>>16);
    endtask
    task sa_wait(input [15:0] v);
        integer polls;
        polls=0;
        do begin bus(0,0,RQ+2,0,0);polls++;if(polls>500000)$fatal(1,"RQ SA timeout want=%h got=%h SERV PC=%h",v,value,dut.disk.ia);end while(value!=v);
    endtask
    task rq_init;
        put(RQ,0);sa_wait(16'h0b40);
        put(RQ+2,16'h899b);sa_wait(16'h1089);
        wait(irq_valid);check(irq_priority==5 && irq_vector==16'o154,"RQ step 2 interrupt");acknowledge;
        put(RQ+2,COMM&65535);sa_wait(16'h209b);wait(irq_valid);acknowledge;
        put(RQ+2,COMM>>16);sa_wait(16'h4133);wait(irq_valid);acknowledge;
        for(integer j=-4;j<16;j+=2)expect_word(COMM+j,0);
        put(RQ+2,1);expect_word(RQ+2,0);ring_index=0;
    endtask
    task packet(input [7:0] opcode,input integer unit,input integer bytes,input integer ba,input integer lbn);
        for(integer j=0;j<64;j+=2)put(CMD-4+j,0);
        put(CMD-4,32);put(CMD,16'h1234);put(CMD+2,16'h5678);put(CMD+4,unit);
        put(CMD+8,opcode);put(CMD+12,bytes);put(CMD+14,bytes>>16);
        put(CMD+16,ba);put(CMD+18,ba>>16);put(CMD+28,lbn);put(CMD+30,lbn>>16);
    endtask
    task dispatch(input [7:0] opcode,input [15:0] status);
        integer polls;
        put(COMM+4*ring_index,RSP&65535);put(COMM+4*ring_index+2,(RSP>>16)|16'h8000);
        put(COMM+8+4*ring_index,CMD&65535);put(COMM+10+4*ring_index,(CMD>>16)|16'h8000);
        bus(0,0,RQ,0,0);polls=0;
        do begin bus(0,0,COMM+4*ring_index+2,0,0);polls++;
            if(polls>1000000)begin
                for(integer t=0;t<128;t++)$display("SERV %h %h",trace_pc[(trace_index+t)%128],trace_instruction[(trace_index+t)%128]);
                $fatal(1,"RQ packet timeout op=%o SERV PC=%h engine=%d",opcode,dut.disk.ia,dut.disk.engine.state);end
        end while(value[15]);
        wait(irq_valid);
        expect_word(RSP,16'h1234);expect_word(RSP+2,16'h5678);
        expect_word(RSP+8,{8'b0,opcode}|16'h80);expect_word(RSP+10,status);
        expect_word(COMM-4,1);expect_word(COMM-2,1);
        expect_word(COMM+10+4*ring_index,(CMD>>16)|16'h4000);
        repeat(5000)@(negedge clk);
        check(irq_valid && irq_vector==16'o154,"RQ completion IRQ");acknowledge;
        ring_index=1-ring_index;
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        do bus(0,0,22'o17777504,0,0);while(!value[15]);
        wait(!dut.disk.owner);
        check(value==16'ha008,"RK0 boot metadata");
        expect_word(RK,16'o4720);expect_word(RK+4,16'o200);
        bus(0,0,22'o17774400,0,1);bus(0,0,22'o17777440,0,1); // absent controllers do not alias RK
        rk_io(16'o105,256,16'h8000,0);rk_done(0);
        check(irq_valid && irq_vector==16'o220,"RK completion IRQ");acknowledge;
        for(integer j=0;j<512;j+=2)check(word_at('h8000+j)=={pattern(0,j+1),pattern(0,j)},"RK0 data");
        rk_io(5,256,16'h9000,16'he000);rk_done(0);
        for(integer j=0;j<512;j+=2)check(word_at('h9000+j)=={pattern(100,j+1),pattern(100,j)},"RK7 separate partition");
        rk_io(3,1,16'h8000,16'he000);rk_done(16'o20000); // readonly
        put(RK+4,1);put('ha000,16'habcd);rk_io(3,1,16'ha000,1);rk_done(0);
        rk_io(5,256,16'ha200,1);rk_done(0);check(word_at('ha200)==16'habcd,"RK short write data");
        check(word_at('ha202)=={pattern(1,3),pattern(1,2)},"RK short write preserves tail");
        rk_io(7,1,16'h8000,1);rk_done(1); // write-check mismatch is soft
        rk_io(5,1,16'ha400,1);rk_done(0);expect_word(RK+6,0);
        rk_io(5,1,16'ha400,12);rk_done(16'o40);put(RK+4,1);
        // Identification and the complete CSR bank are served by real SERV.
        expect_word(22'o17777750,16'o1045);expect_word(22'o17777752,16'o10);
        put(22'o17777750,16'hffff);expect_word(22'o17777750,16'o1045);
        for(integer page=0;page<32;page++)begin
            expect_word(UBM+4*page,0);expect_word(UBM+4*page+2,0);
            map_page(page,22'h3fffff);
            expect_word(UBM+4*page,16'hfffe);expect_word(UBM+4*page+2,16'h3f);
        end
        bus(1,1,UBM,16'h0055,0);expect_word(UBM,16'hff54);
        bus(1,1,UBM+1,16'hab00,0);expect_word(UBM,16'hab54);
        bus(1,1,UBM+3,16'hff00,0);expect_word(UBM+2,16'h003f);
        bus(1,1,UBM+2,16'h0085,0);expect_word(UBM+2,16'h0005);
        // Disabled BME retains 18-bit RK DMA, even with nonidentity maps.
        rk_io(5,1,'h18000,0);rk_done(0);
        check(word_at('h18000)=={pattern(0,1),pattern(0,0)},"BME disabled uses physical 18-bit address");
        map_page(8,22'h180002);map_page(9,22'h120000);map_page(10,22'h190000);
        put('h11ff0,16'h5678);dma_map_enabled=1;
        rk_io(5,16,'h11ff0,0);rk_done(0);
        for(integer j=0;j<32;j+=2)begin
            check(word_at(j<16?'h181ff2+j:'h120000+j-16)=={pattern(0,j+1),pattern(0,j)},"mapped RK crosses noncontiguous pages");
        end
        check(word_at('h11ff0)==16'h5678,"mapped DMA does not alias bus address");
        expect_word(RK+8,16'h2010);expect_word(RK+6,0);
        // A partial RK write preserves the tail of its SD sector across a map split.
        rk_io(3,16,'h11ff0,2);rk_done(0);
        rk_io(5,256,'h14000,2);rk_done(0);
        for(integer j=0;j<512;j+=2)begin
            check(word_at('h190000+j)=={pattern(j<32?0:2,j+1),pattern(j<32?0:2,j)},"mapped RK write preserves sector tail");
        end
        rk_io(7,16,'h11ff0,0);rk_done(0);
        put('h181ff2,16'hbabe);rk_io(7,16,'h11ff0,0);rk_done(1); // mismatch in first fragment must survive second
        rk_io(16'o4005,8,'h10000,0);rk_done(0);
        check(word_at('h180002)=={pattern(0,15),pattern(0,14)},"mapped inhibited DMA stays on one word");
        expect_word(RK+8,0);expect_word(RK+6,0);
        put('h100,16'hcafe);map_page(10,22'h200000);
        rk_io(5,1,'h14100,0);rk_done(16'o2000);put(RK+4,1);
        check(word_at('h100)==16'hcafe,"mapped NXM never aliases low SRAM");
        map_page(31,0);rk_io(5,1,'h3e100,0);rk_done(16'o2000);put(RK+4,1);
        check(word_at('h100)==16'hcafe,"UNIBUS I/O page never aliases SRAM");
        map_page(10,22'h3ffffe);rk_io(5,1,'h14004,0);rk_done(0);
        check(word_at(2)=={pattern(0,1),pattern(0,0)},"map addition wraps at 22 bits");
        @(negedge clk);peripheral_reset=1;@(negedge clk);peripheral_reset=0;
        for(integer page=0;page<32;page++)begin
            expect_word(UBM+4*page,0);expect_word(UBM+4*page+2,0);
        end
        // BME remains high here: RQ ring and data must bypass the zeroed map.
        // Full UQSSP handshake, 22-bit rings and data, two-slot wrap.
        rq_init;
        packet(4,0,0,0,0);dispatch(4,0);expect_word(RSP-2,8);
        packet(3,0,0,0,0);dispatch(3,4);
        packet(8'o11,0,0,0,0);dispatch(8'o11,0);
        expect_word(RSP+36,16'hbfa0);expect_word(RSP+38,4); // RD54: 311200 blocks
        packet(8'o41,0,512,DATA,0);dispatch(8'o41,0);
        for(integer j=0;j<512;j+=2)check(word_at(DATA+j)=={pattern(200,j+1),pattern(200,j)},"RQ0 22-bit DMA");
        packet(8'o11,1,0,0,0);dispatch(8'o11,0);
        packet(8'o41,1,512,DATA+512,0);dispatch(8'o41,0);
        for(integer j=0;j<512;j+=2)check(word_at(DATA+512+j)=={pattern(300,j+1),pattern(300,j)},"RQ1 separate partition");
        packet(8'o42,1,512,DATA,0);dispatch(8'o42,16'h2006);
        put(DATA,16'hbeef);packet(8'o42,0,2,DATA,1);dispatch(8'o42,0);
        packet(8'o41,0,512,DATA+1024,1);dispatch(8'o41,0);
        check(word_at(DATA+1024)==16'hbeef && word_at(DATA+1026)==0,"MSCP short write zero padding");
        packet(8'o40,0,512,DATA,1);dispatch(8'o40,7);
        packet(8'o41,0,2,22'h200000,0);dispatch(8'o41,16'h69);
        packet(8'o41,0,2,DATA+1,0);dispatch(8'o41,16'h29);
        packet(8'o41,0,3,DATA,0);dispatch(8'o41,16'h49);
        packet(8'o41,0,512,DATA,311200+7);dispatch(8'o41,16'h1c01);
        packet(8'o11,3,0,0,0);dispatch(8'o11,16'h23);
        packet(8'o77,0,0,0,0);dispatch(8'o77,16'h0801);
        put(RQ,0);sa_wait(16'h0b40);check(!irq_valid,"RQ reset clears interrupt");
        rq_init;packet(8'o41,0,512,DATA,0);dispatch(8'o41,4); // reset makes units available, not online
        $display("PASS MMU RK/RQ service: %0d checks",checks);$finish;
    end
    initial begin #2000000000;$fatal(1,"global timeout SERV PC=%h",dut.disk.ia);end
    reg [15:0] value;integer writes_before;
    task bus(input bit wr,input bit by,input [21:0] addr,input [15:0] data,input bit fault);
        begin
            @(negedge clk);request=1;writing=wr;byte_access=by;address=addr;write_data=data;n=0;
            do begin @(posedge clk);n++;if(n>10000000)begin for(integer t=0;t<128;t++)$display("SERV %h %h",trace_pc[(trace_index+t)%128],trace_instruction[(trace_index+t)%128]);$fatal(1,"bus timeout address=%o write=%b data=%o SERV=%h dc=%b da=%h bridge=%h owner=%b engine=%d",addr,wr,data,dut.disk.ia,dut.disk.dc,dut.disk.da,dut.disk.bridge_data,dut.disk.owner,dut.disk.engine.state);end end while(!ready);
            check(error==fault,"physical response");value=read_data;
            if($test$plusargs("TRACE_IO") && addr[21])$display("IO %o %b %o -> %o at %t",addr,wr,data,value,$time);
            @(negedge clk);request=0;repeat(3)@(negedge clk);
        end
    endtask
endmodule
