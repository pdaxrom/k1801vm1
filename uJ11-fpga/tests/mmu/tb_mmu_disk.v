`timescale 1ns/1ps
// Real SERV instructions, SPI wire protocol, SRAM pins and a competing CPU
// master. No injected IOP PC/state and no direct controller completions.
module tb_mmu_disk;
`ifdef UJ11_IOP_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,power_on=1;
    always #20.833 clk=~clk;
    reg rr=0,rw=0;reg [3:0] ra=0;reg [1:0] rl=3;reg [15:0] wd=0;
    wire [15:0] rd;wire ready,irq;reg irq_ack=0;
    reg sr=0,sw=0;reg [1:0] sa=0;reg [15:0] sd=0;
    wire [15:0] srd;wire sready,serror,cs,sck,mosi,miso;
    wire dr,dw,dready;wire [21:0] da;wire [15:0] dd;
    wire mr,mw,mready;wire [19:0] ma;wire [1:0] ml;wire [15:0] md,mrd;
    wire initialized;wire [19:0] pa;wire [15:0] pd;
    wire ce_n,oe_n,we_n,lb_n,ub_n;
    reg cr=0,cw=0;reg [19:0] ca=0;reg [15:0] cd=0;wire cready;
    reg traffic=0;integer cpu_cycles=0,dma_cycles=0,checks=0;
    reg absent=0,fail_read=0,fail_write=0,stuck_busy=0;
    uj11_mmu_disk #(.CLOCK_HZ(240000),.SD_SLOW_DIV(4),.SD_FAST_DIV(2)) disk(
        .clk(clk),.reset(reset),.bus_reset(1'b0),.rk_request(rr),.rk_write(rw),
        .rk_address(ra),.rk_lanes(rl),.rk_wdata(wd),.rk_rdata(rd),.rk_ready(ready),.rk_irq(irq),.rk_irq_ack(irq_ack),
        .storage_enabled(),.io_request(1'b0),.io_address(13'b0),.io_rdata(),.io_ready(),.io_error(),.io_irq(),.io_vector(),.io_irq_ack(1'b0),
        .sd_request(sr),.sd_write(sw),.sd_byte(1'b0),.sd_address(sa),.sd_wdata(sd),
        .sd_rdata(srd),.sd_ready(sready),.sd_error(serror),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),
        .dma_request(dr),.dma_write(dw),.dma_unibus(),.dma_address(da),.dma_data(dd),
        .dma_ready(dready),.dma_error(1'b0),.dma_rdata(mrd));
    uj11_mmu_sram_arbiter arbiter(.clk(clk),.reset(reset),
        .cpu_request(cr),.cpu_write(cw),.cpu_address(ca),.cpu_lanes(2'b11),.cpu_data(cd),.cpu_ready(cready),
        .dma_request(dr),.dma_write(dw),.dma_address(da),.dma_data(dd),.dma_ready(dready),
        .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    uj11_sram memory(.clk(clk),.power_on(power_on),.reset(reset),.request(mr),.write(mw),
        .address(ma),.byte_enable(ml),.write_data(md),.read_data(mrd),.ready(mready),.initialized(initialized),
        .sram_address(pa),.sram_data(pd),.sram_ce_n(ce_n),.sram_oe_n(oe_n),
        .sram_we_n(we_n),.sram_lb_n(lb_n),.sram_ub_n(ub_n));
    async_sram_model sram(.address(pa),.data(pd),.ce_n(ce_n),.oe_n(oe_n),.we_n(we_n),.lb_n(lb_n),.ub_n(ub_n));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(absent),
        .fail_read(fail_read),.fail_write(fail_write),.stuck_busy(stuck_busy),
        .bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s (IOP PC=%h CS1=%o ER=%o)",why,disk.ia,disk.registers.cs1,disk.registers.er);checks++;end
    endtask
    task rk(input bit wr,input [3:0] a,input [15:0] v,input [1:0] lanes,output [15:0] result);
        begin
            @(negedge clk);rr=1;rw=wr;ra=a;wd=v;rl=lanes;
            do @(negedge clk);while(!ready);
            result=rd;rr=0;@(negedge clk);
        end
    endtask
    reg [15:0] result;
    task put(input [3:0] a,input [15:0] v);rk(1,a,v,3,result);endtask
    task expect_reg(input [3:0] a,input [15:0] v,input string why);
        begin rk(0,a,0,3,result);check(result===v,why);end
    endtask
    task spi(input bit wr,input [1:0] a,input [15:0] v,output [15:0] result);
        begin
            @(negedge clk);sr=1;sw=wr;sa=a;sd=v;
            do @(negedge clk);while(!sready);
            check(!serror,"legacy SPI response");result=srd;sr=0;@(negedge clk);
        end
    endtask
    task spi_out(input [7:0] v);spi(1,0,{8'b0,v},result);endtask
    task spi_in;spi(0,0,0,result);endtask
    task select_card(input bit selected);spi(1,2,selected ? 0 : 1,result);endtask
    task command(input [7:0] op,input [31:0] arg,input [7:0] crc,input [7:0] expected);
        begin
            select_card(1);spi_out(op);spi_out(arg[31:24]);spi_out(arg[23:16]);
            spi_out(arg[15:8]);spi_out(arg[7:0]);spi_out(crc);
            do spi_in();while(result==255);
            check(result==expected,"SD initialization response");
        end
    endtask
    task close_card;begin select_card(0);spi_in();end endtask
    task wait_done;
        integer n;
        begin
            n=0;
            do begin rk(0,0,0,3,result);n++;if(n>500000)$fatal(1,"RK timeout PC=%h CS1=%o",disk.ia,result);end
            while(!result[7]);
            repeat(10)@(negedge clk);
        end
    endtask
    task start(input [15:0] command_word,input [15:0] count,input [15:0] addr,input [15:0] disk_addr,input [15:0] cylinder);
        begin put(1,-count);put(2,addr);put(3,disk_addr);put(8,cylinder);put(0,command_word);end
    endtask
    function [15:0] word_at(input integer a);word_at={sram.memory[a+1],sram.memory[a]};endfunction
    function [7:0] pattern(input integer lba,input integer offset);pattern=8'((lba*17)^(offset*3)^(offset>>8));endfunction
    // Competing CPU transfers use separate high physical RAM. Every read is checked.
    initial begin
        wait(!reset);
        forever begin
            wait(traffic);@(negedge clk);cr=1;cw=1;ca=20'h80010;cd=16'hbeef;
            do @(negedge clk);while(!cready);
            cr=0;repeat(3)@(negedge clk);
            cr=1;cw=0;
            do @(negedge clk);while(!cready);
            check(mrd===16'hbeef,"CPU SRAM read during disk transfer");cpu_cycles++;
            cr=0;repeat(3)@(negedge clk);
        end
    end
    always @(posedge clk)if(dr && dready)dma_cycles<=dma_cycles+1;
    integer before_cpu,before_dma,before_writes;
    initial begin
        for(integer l=0;l<256;l++)for(integer j=0;j<512;j++)card.memory[l*512+j]=pattern(l,j);
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        expect_reg(0,16'o200,"power-on DONE");expect_reg(5,16'o100701,"drive signature");
        for(integer i=0;i<10;i++)spi_out(255);
        command('h40,0,'h95,1);close_card();
        command('h48,'h1aa,'h87,1);repeat(4)spi_in();close_card();
        command('h77,0,1,1);close_card();command('h69,'h40000000,1,1);close_card();
        command('h77,0,1,1);close_card();command('h69,'h40000000,1,0);close_card();
        command('h7a,0,1,0);repeat(4)spi_in();close_card();
        spi(1,2,3,result);
        put(0,16'o103);wait_done();check(irq,"PACK ACK interrupt");
        @(negedge clk);irq_ack=1;@(negedge clk);irq_ack=0;check(!irq,"interrupt acknowledge");
        put(0,16'o13);wait_done();check(result[14],"RECAL drive attention");
        put(4,16'o40);expect_reg(0,16'o200,"CS2 clear");
        rk(1,2,16'hab00,2,result);rk(1,2,16'h0034,1,result);expect_reg(2,16'hab34,"byte lanes");

        traffic=1;before_cpu=cpu_cycles;before_dma=dma_cycles;
        start(16'o121,256,16'h8000,5,0);wait_done();
        check(!result[15] && irq,"READ done/IRQ");
        check(cpu_cycles>before_cpu+20,"main CPU advances concurrently");
        check(dma_cycles-before_dma==256,"one sector DMA word count");
        for(integer j=0;j<512;j++)check(sram.memory[16'h8000+j]===pattern(5,j),"sector READ bytes");
        expect_reg(1,0,"READ WC");expect_reg(2,16'h8200,"READ BA");expect_reg(3,6,"READ DA");
        put(4,16'o40);
        start(16'o21,3,16'o177440,7,0);wait_done();
        for(integer j=0;j<6;j++)check(sram.memory[16'o177440+j]===pattern(7,j),"DMA reaches physical I/O page");
        check(word_at(65536+16'o177440)==0,"low-address DMA does not alias RAM above 64 KiB");
        expect_reg(1,0,"I/O-page DMA did not overwrite WC CSR");expect_reg(3,8,"partial sector advances DA");
        start(16'o21,300,16'h9000,16'o1025,0);wait_done();
        for(integer j=0;j<600;j++)check(sram.memory[16'h9000+j]===pattern(j<512?65:66,j%512),"multi-sector head/cylinder rollover");
        expect_reg(8,1,"cylinder rollover");expect_reg(3,1,"next cylinder sector");

        start(16'o23,3,16'o177440,9,0);wait_done();check(!result[15],"partial WRITE status");
        for(integer j=0;j<512;j++)check(card.memory[9*512+j]===(j<6?pattern(7,j):8'b0),"partial WRITE zero padding");
        start(16'o21,256,16'ha000,9,0);wait_done();
        for(integer j=0;j<512;j++)check(sram.memory[16'ha000+j]===card.memory[9*512+j],"write/read round trip");

        start(16'o23,300,16'h9000,16'h0108,0);wait_done();
        check(!result[15],"multi-sector WRITE status");
        for(integer j=0;j<1024;j++)check(card.memory[30*512+j]===
            (j<512 ? pattern(65,j) : j<600 ? pattern(66,j-512) : 8'b0),
            "multi-sector WRITE data and final zero padding");
        expect_reg(1,0,"multi-sector WRITE WC");expect_reg(2,16'h9258,"multi-sector WRITE BA");
        expect_reg(3,16'h010a,"multi-sector WRITE DA");

        before_dma=dma_cycles;card.corrupt_read_crc=1;
        start(16'o121,256,16'hb000,4,0);wait_done();
        check(result[15] && irq,"bad CRC completion");expect_reg(6,16'o100000,"CRC error bit");
        check(dma_cycles==before_dma,"bad CRC does not reach guest memory");card.corrupt_read_crc=0;put(0,16'o100000);
        expect_reg(0,16'o200,"CS1 controller clear");check(!irq,"clear drops IRQ");
        fail_read=1;start(16'o21,256,16'hb000,4,0);wait_done();check(result[15],"read error token");fail_read=0;put(4,16'o40);
        fail_write=1;start(16'o23,256,16'h8000,10,0);wait_done();check(result[15],"rejected write token");fail_write=0;put(4,16'o40);
        stuck_busy=1;start(16'o23,256,16'h8000,10,0);wait_done();check(result[15] && cs,"bounded programming timeout");stuck_busy=0;put(4,16'o40);
        card.no_write_response=1;start(16'o23,256,16'h8000,10,0);wait_done();check(result[15] && cs,"missing write response timeout");card.no_write_response=0;put(4,16'o40);
        card.no_read_token=1;start(16'o21,256,16'hb000,4,0);wait_done();check(result[15] && cs,"missing read token timeout");card.no_read_token=0;put(4,16'o40);
        absent=1;start(16'o21,256,16'hb000,4,0);wait_done();check(result[15] && cs,"absent card timeout");absent=0;put(4,16'o40);

        start(16'o121,256,16'hb000,4,0);wait(dr && dready);put(4,16'o40);
        repeat(100)@(negedge clk);before_dma=dma_cycles;
        repeat(10000)@(negedge clk);
        check(dma_cycles==before_dma && !irq && cs,"clear cancels DMA and stale completion");
        expect_reg(0,16'o200,"clear during DMA DONE");
        start(16'o21,256,16'hb000,4,0);wait_done();check(!result[15],"recovery after cancellation");

        // A legacy selection cannot be stolen halfway through a transaction.
        select_card(1);start(16'o21,256,16'hb000,4,0);
        repeat(5000)@(negedge clk);check(!disk.owner && !cs,"legacy SD retains selection");
        close_card();wait_done();check(!result[15],"ownership acquired after legacy release");
        start(16'o21,256,16'hb000,22,0);wait_done();check(result[15],"invalid sector rejected");put(4,16'o40);
        put(4,1);start(16'o21,256,16'hb000,0,0);wait_done();expect_reg(4,16'o10001,"absent drive");put(4,16'o40);
        // Carry within a sector, then across the next firmware sector, must
        // update CS1 A17:A16 as well as the engine's physical SRAM address.
        start(16'o21,300,16'hfff0,4,0);wait_done();
        check(!result[15] && result[9:8]==1,"DMA carry across 64 KiB");
        for(integer j=0;j<600;j++)check(sram.memory['hfff0+j]===pattern(j<512?4:5,j%512),"DMA 64 KiB crossing bytes");
        expect_reg(2,16'h0248,"extended BA after two sectors");
        start(16'o423,300,16'hfff0,10,0);wait_done();
        check(!result[15] && result[9:8]==2,"DMA WRITE carry from 128 KiB");
        // Explicit upper address read, and architectural 18-bit wrap.
        start(16'o1421,300,16'hfff0,6,0);wait_done();
        check(!result[15] && result[9:8]==0,"18-bit DMA wrap");
        for(integer j=0;j<600;j++)check(sram.memory[('h3fff0+j)&'h3ffff]===pattern(j<512?6:7,j%512),"DMA wrap bytes");
        expect_reg(2,16'h0248,"wrapped BA");
        start(16'o1423,300,16'hfff0,12,0);wait_done();
        check(!result[15],"high-memory disk WRITE");
        for(integer j=0;j<1024;j++)check(card.memory[12*512+j]===
            (j<512 ? pattern(6,j) : j<600 ? pattern(7,j-512) : 8'b0),"extended WRITE round trip");
        put(4,16'o40);
        start(16'o21,256,16'hb000,4,0);wait_done();check(!result[15],"final successful READ");
        traffic=0;
        $display("PASS MMU SERV disk: %0d checks, %0d CPU transactions, %0d DMA words",checks,cpu_cycles,dma_cycles);
        $finish;
    end
    initial begin #2000000000;$fatal(1,"global timeout IOP PC=%h",disk.ia);end
endmodule
