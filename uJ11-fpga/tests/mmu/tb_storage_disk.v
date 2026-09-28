`timescale 1ns/1ps
// Real SERV instructions, SPI wire protocol, SRAM pins and a competing CPU
// master. No injected IOP PC/state and no direct controller completions.
module tb_storage_disk;
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
    reg metadata=0;reg [15:0] boot_status=0;
    wire io_ready,io_irq,io_error;wire [8:0] io_vector;
    wire [12:0] io_address=metadata ? 13'o17504 : 13'o17440+{ra,1'b0};
    assign ready=io_ready && rr;assign irq=io_irq && io_vector==9'o210;
    uj11_mmu_disk #(.CLOCK_HZ(240000),.SD_SLOW_DIV(4),.SD_FAST_DIV(2)) disk(
        .clk(clk),.reset(reset),.bus_reset(1'b0),.rk_request(1'b0),.rk_write(rw),
        .rk_address(ra),.rk_lanes(rl),.rk_wdata(wd),.rk_rdata(),.rk_ready(),.rk_irq(),.rk_irq_ack(1'b0),
        .storage_enabled(),.io_request(rr),.io_address(io_address),.io_rdata(rd),.io_ready(io_ready),.io_error(io_error),
        .io_irq(io_irq),.io_vector(io_vector),.io_irq_ack(irq_ack),
        .sd_request(sr),.sd_write(sw),.sd_byte(1'b0),.sd_address(sa),.sd_wdata(sd),
        .sd_rdata(srd),.sd_ready(sready),.sd_error(serror),
        .sd_cs_n(cs),.sd_sck(sck),.sd_mosi(mosi),.sd_miso(miso),
        .dma_request(dr),.dma_write(dw),.dma_unibus(),.dma_address(da),.dma_data(dd),
        .dma_ready(dready),.dma_error(1'b0),.dma_rdata(mrd));
    uj11_mmu_sram_arbiter arbiter(.clk(clk),.reset(reset),
        .cpu_lock(1'b0),.cpu_request(cr),.cpu_write(cw),.cpu_address(ca),.cpu_lanes(2'b11),.cpu_data(cd),.cpu_ready(cready),
        .dma_request(dr),.dma_write(dw),.dma_address(da),.dma_data(dd),.dma_ready(dready),
        .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    uj11_sram memory(.clk(clk),.power_on(power_on),.reset(reset),.request(mr),.write(mw),
        .address(ma),.byte_enable(ml),.write_data(md),.read_data(mrd),.ready(mready),.initialized(initialized),
        .sram_address(pa),.sram_data(pd),.sram_ce_n(ce_n),.sram_oe_n(oe_n),
        .sram_we_n(we_n),.sram_lb_n(lb_n),.sram_ub_n(ub_n));
    serv_memory_guard guard(.clk(clk),.reset(disk.iop_reset),
        .write(disk.data_accept && disk.memory_selected && disk.de),.address(disk.da));
    async_sram_model sram(.address(pa),.data(pd),.ce_n(ce_n),.oe_n(oe_n),.we_n(we_n),.lb_n(lb_n),.ub_n(ub_n));
    spi_sd_model card(.cs_n(cs),.sck(sck),.mosi(mosi),.miso(miso),.absent(absent),
        .fail_read(fail_read),.fail_write(fail_write),.stuck_busy(stuck_busy),
        .bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"%s (IOP PC=%h CS1=%o ER=%o)",why,disk.ia,rd,16'b0);checks++;end
    endtask
    task rk(input bit wr,input [3:0] a,input [15:0] v,input [1:0] lanes,output [15:0] result);
        begin
            @(negedge clk);rr=1;rw=wr;ra=a;wd=v;rl=lanes;
            do @(negedge clk);while(!ready);
            result=rd;rr=0;@(negedge clk);
        end
    endtask
    task wait_boot_status;
        begin
            metadata=1;
            do begin rk(0,0,0,3,boot_status);end while(!boot_status[15]);
            metadata=0;
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
    integer before_dma,before_writes,expected_error;
    task initialized_iop;
        begin
            wait_boot_status();repeat(10)@(negedge clk);
            check(boot_status==16'h8007,"partition table selected boot unit 7");
            wait(!disk.owner);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        if($value$plusargs("FAIL_INIT=%d",expected_error))begin
            wait_boot_status();repeat(10)@(negedge clk);
            check(boot_status==(16'hc000|expected_error),"startup error diagnostic");
            expect_reg(5,0,"bad label exposes no drive");
            put(0,16'o23);check(io_error,"bad label rejects controller access with NXM");
            repeat(10000)@(negedge clk);
            check(card.writes==0 && dma_cycles==0,"invalid label cannot write or DMA");
            $display("PASS MMU storage bad label: %0d checks",checks);$finish;
        end
        initialized_iop();
        check(dma_cycles==0,"startup does not touch guest SRAM");
        expect_reg(5,16'o100701,"unit 0 present");
        put(4,7);expect_reg(5,16'o104701,"unit 7 write protected");
        put(4,1);expect_reg(5,0,"absent unit status");
        start(16'o21,256,16'h8000,5,0);wait_done();expect_reg(4,16'o10001,"absent unit command");
        put(4,0);traffic=1;
        start(16'o121,256,16'h8000,5,0);wait_done();check(!result[15] && irq,"unit 0 read");
        for(integer j=0;j<512;j++)check(sram.memory[16'h8000+j]===pattern(5,j),"unit 0 partition offset");
        put(4,7);start(16'o21,256,16'ha000,5,0);wait_done();check(!result[15],"unit 7 read");
        for(integer j=0;j<512;j++)check(sram.memory[16'ha000+j]===pattern(105,j),"unit 7 distinct contents");
        before_writes=card.writes;
        start(16'o23,256,16'h8000,5,0);wait_done();expect_reg(6,16'o4000,"write lock error");
        check(card.writes==before_writes,"protected unit unchanged");
        put(4,0);start(16'o23,256,16'ha000,9,0);wait_done();check(!result[15],"unit 0 write");
        check(card.dirty_lba[0]==2048+9,"write uses partition base");
        start(16'o21,256,16'h9000,9,0);wait_done();
        for(integer j=0;j<512;j++)check(sram.memory[16'h9000+j]===pattern(105,j),"read back partition write");
        before_writes=card.writes;before_dma=dma_cycles;
        start(16'o23,257,16'h8000,16'h0215,814);wait_done();expect_reg(6,16'o2000,"request crosses end of disk");
        check(card.writes==before_writes && dma_cycles==before_dma,"bounds checked before first write");
        before_dma=dma_cycles;card.corrupt_read_crc=1;
        start(16'o21,256,16'hb000,4,0);wait_done();expect_reg(6,16'o100000,"CRC error");
        check(dma_cycles==before_dma,"bad sector CRC cannot reach SRAM");card.corrupt_read_crc=0;
        // Writing only CS1's low byte must not turn the old high ERR bit into
        // a controller-clear command. MOVB/BISB on IE is common in drivers.
        rk(1,0,16'o100,2'b01,result);
        expect_reg(6,16'o100000,"byte IE write retains error");
        expect_reg(1,-256,"byte IE write retains residual count");
        expect_reg(2,16'hb000,"byte IE write retains DMA address");
        rk(0,0,0,2'b11,result);check(result[15] && result[7] && result[6],"byte IE write retains ERR and READY");
        rk(1,0,16'o100000,2'b10,result);
        expect_reg(6,0,"high-byte controller clear");expect_reg(1,0,"clear resets residual count");
        // Reset while DMA is active: cancel only this controller generation;
        // preserve metadata and reject the old completion and remaining DMA.
        start(16'o121,256,16'hb000,4,0);wait(dr && dready);put(4,16'o40);
        repeat(100)@(negedge clk);before_dma=dma_cycles;
        initialized_iop();
        check(dma_cycles==before_dma && !irq,"cancel kills old DMA and IRQ");
        start(16'o21,256,16'hb000,4,0);wait_done();check(!result[15],"read after cancellation");
        // Architectural 18-bit DMA wrap is independent of SD partitioning.
        start(16'o1421,300,16'hfff0,6,0);wait_done();check(!result[15] && result[9:8]==0,"18-bit DMA wrap");
        for(integer j=0;j<600;j++)check(sram.memory[('h3fff0+j)&'h3ffff]===pattern(j<512?6:7,j%512),"wrapped bytes");
        check(cpu_cycles>100,"CPU continues during storage service");traffic=0;
        $display("PASS MMU storage: %0d checks, %0d CPU transactions, %0d DMA words",checks,cpu_cycles,dma_cycles);$finish;
    end
    initial begin #8000000000;$fatal(1,"global timeout IOP PC=%h status=%h",disk.ia,boot_status);end
endmodule
