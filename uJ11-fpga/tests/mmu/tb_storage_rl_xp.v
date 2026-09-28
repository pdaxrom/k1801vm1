`timescale 1ns/1ps
// Real SERV instructions, SPI wire protocol, SRAM pins and a competing CPU
// master. No injected IOP PC/state and no direct controller completions.
module tb_storage_rl_xp;
`ifdef UJ11_IOP_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,reset=1,power_on=1,bus_reset=0;
    always #20.833 clk=~clk;
    reg lr=0,xr=0;reg [4:0] storage_address=0;
    wire [15:0] lrd,xrd;wire lready,xready,lirq,xirq;reg lack=0,xack=0;
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
    wire io_ready,io_irq;wire [8:0] io_vector;
    wire [12:0] io_address=metadata ? 13'o17504 : lr ? 13'o14400+{storage_address,1'b0} : xr ? 13'o16700+{storage_address,1'b0} : 13'o17440+{ra,1'b0};
    assign ready=io_ready && rr;assign irq=io_irq && io_vector==9'o210;
    assign lrd=rd;assign xrd=rd;assign lready=io_ready && lr;assign xready=io_ready && xr;assign lirq=io_irq && io_vector==9'o160;assign xirq=io_irq && io_vector==9'o254;
    uj11_mmu_disk #(.CLOCK_HZ(240000),.SD_SLOW_DIV(4),.SD_FAST_DIV(2)) disk(
        .clk(clk),.reset(reset),.bus_reset(bus_reset),.rk_request(1'b0),.rk_write(rw),
        .rk_address(ra),.rk_lanes(rl),.rk_wdata(wd),.rk_rdata(),.rk_ready(),.rk_irq(),.rk_irq_ack(1'b0),
        .storage_enabled(),.io_request(rr || lr || xr),.io_address(io_address),.io_rdata(rd),.io_ready(io_ready),.io_error(),
        .io_irq(io_irq),.io_vector(io_vector),.io_irq_ack(irq_ack || lack || xack),
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
    task access(input bit xp,input bit wr,input [4:0] a,input [15:0] v);
        begin
            @(negedge clk);lr=!xp;xr=xp;rw=wr;storage_address=a;wd=v;rl=3;
            do @(negedge clk);while(!(xp?xready:lready));
            result=xp?xrd:lrd;lr=0;xr=0;@(negedge clk);
        end
    endtask
    task put_rl(input [4:0] a,input [15:0] v);access(0,1,a,v);endtask
    task put_xp(input [4:0] a,input [15:0] v);access(1,1,a,v);endtask
    task expect_ctl(input bit xp,input [4:0] a,input [15:0] v,input string why);
        begin access(xp,0,a,0);if(result!==v)$display("got %o wanted %o",result,v);check(result===v,why);end
    endtask
    task done(input bit xp,input bit error_expected);
        integer n;
        begin
            n=0;
            do begin access(xp,0,0,0);n++;if(n>1000000)$fatal(1,"controller timeout XP=%b PC=%h CS=%o",xp,disk.ia,result);end while(!result[7]);
            if(result[15]!==error_expected)$display("XP=%b CS=%o",xp,result);
            check(result[15]===error_expected,"command error status");
        end
    endtask
    task rl_io(input [15:0] cmd,input integer count,input integer addr,input [15:0] sector);
        begin put_rl(1,addr);put_rl(2,sector);put_rl(3,-count);put_rl(4,addr>>16);put_rl(0,cmd|((addr>>12)&16'o60));end
    endtask
    task xp_io(input [15:0] cmd,input integer count,input integer addr,input [15:0] sector,input [15:0] cylinder);
        begin put_xp(1,-count);put_xp(2,addr);put_xp(3,sector);put_xp(14,cylinder);put_xp(20,addr>>16);put_xp(0,cmd|((addr>>8)&16'o1400));end
    endtask
    task bytes_at(input integer a,input integer n,input integer sector,input integer offset,input string why);
        for(integer i=0;i<n;i++)check(sram.memory[a+i]===pattern(sector+(offset+i)/512,(offset+i)%512),why);
    endtask
    integer before_dma,before_writes,slot;
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        wait_boot_status();wait(!disk.owner);
        check(boot_status==16'ha028,"RL0 default and menu");traffic=1;
        expect_ctl(0,0,16'o201,"RL ready and present");
        put_rl(2,16'o13);put_rl(0,4);done(0,0);expect_ctl(0,3,16'o235,"RL02 status");
        put_rl(0,16'o200|16'o1400);expect_ctl(0,0,16'o1600,"absent RL3");
        put_rl(2,16'o13);put_rl(0,16'o1004);done(0,0);expect_ctl(0,3,16'o20235,"RL2 read only status");
        rl_io(16'o114,256,'h120000,0);done(0,0);check(lirq,"RL IRQ completion");
        bytes_at('h120000,512,0,0,"22-bit RL DMA");expect_ctl(0,4,'h12,"RL BAE");
        @(negedge clk);lack=1;@(negedge clk);lack=0;check(!lirq,"RL IRQ ack");
        rl_io(16'o414,128,'h130000,1);done(0,0);bytes_at('h130000,256,100,256,"RL1 partition and odd sector");
        // One short RL sector: zero tail, preserve adjacent SD half.
        rl_io(16'o12,10,'h130000,5);done(0,0);
        rl_io(16'o14,256,'h140000,4);done(0,0);
        bytes_at('h140000,256,2,0,"RL neighbour preserved");bytes_at('h140100,20,100,256,"RL short write data");
        for(integer j=20;j<256;j++)check(sram.memory['h140100+j]==0,"RL short write zero tail");
        rl_io(16'o2,128,'h140100,5);done(0,0);
        sram.memory['h140101]^=1;rl_io(16'o2,128,'h140100,5);done(0,1);
        before_writes=card.writes;rl_io(16'o1012,128,'h130000,0);done(0,1);check(card.writes==before_writes,"RL write protected");
        // Track boundary makes progress through sector 39, then OPI.
        rl_io(16'o14,256,'h150000,39);done(0,1);expect_ctl(0,3,-128,"RL residual at track boundary");
        put_rl(2,(7<<7)|5|16);put_rl(0,6);done(0,0);
        put_rl(0,8);done(0,0);access(0,0,3,0);check((result&16'o177700)==((7<<7)|64),"RL header position");
        expect_ctl(0,3,0,"RL header zero word");access(0,0,3,0);check(result!=0,"RL header CRC word");
        put_rl(2,(7<<7)|1);put_rl(0,6);done(0,0);
        expect_ctl(1,11,16'o20027,"RM05 drive type");
        put_xp(0,16'o21);done(1,0);access(1,0,5,0);check(result[6],"RM05 volume valid");
        xp_io(16'o171,256,'h160000,5,0);done(1,0);check(xirq,"XP IRQ completion");
        bytes_at('h160000,512,205,0,"XP partition and 22-bit DMA");
        @(negedge clk);xack=1;@(negedge clk);xack=0;check(!xirq,"XP IRQ ack");
        put_xp(0,16'o100);check(!xirq,"XP IE rewrite cannot repeat acknowledged DONE IRQ");
        put_xp(0,0);put_xp(0,16'o100);check(xirq,"XP enabling IE on DONE requests one IRQ");
        @(negedge clk);xack=1;@(negedge clk);xack=0;
        put_xp(0,16'o105);done(1,1);check(xirq,"XP seek attention IRQ");
        expect_ctl(1,7,1,"XP seek attention summary");
        @(negedge clk);xr=1;rw=1;storage_address=7;wd=16'hff00;rl=2;
        do @(negedge clk);while(!xready);
        xr=0;@(negedge clk);
        expect_ctl(1,7,1,"XP high byte AS write preserves attention");put_xp(7,1);
        access(1,0,0,0);check(!result[15],"XP clearing attention clears CS1 SC");
        @(negedge clk);xack=1;@(negedge clk);xack=0;
        put_xp(0,16'o100);check(!xirq,"XP attention handler IE rewrite stays acknowledged");
        xp_io(16'o61,10,'h130000,6,0);done(1,0);
        xp_io(16'o71,256,'h170000,6,0);done(1,0);
        bytes_at('h170000,20,100,256,"XP short write data");bytes_at('h170014,492,206,20,"XP short write preserves tail");
        xp_io(16'o51,256,'h170000,6,0);done(1,0);
        sram.memory['h170001]^=1;xp_io(16'o51,256,'h170000,6,0);done(1,1);
        put_xp(4,8);xp_io(16'o71,256,'h180000,7,0);done(1,0);
        check(word_at('h180000)=={pattern(207,511),pattern(207,510)},"XP inhibit DMA address");
        expect_ctl(1,2,0,"XP inhibited BA unchanged");put_xp(4,0);
        before_writes=card.writes;before_dma=dma_cycles;
        xp_io(16'o61,257,'h130000,'h121f,822);done(1,1);
        check(card.writes==before_writes && dma_cycles==before_dma,"XP bounds before write");
        xp_io(16'o71,256,'h200000,0,0);done(1,1);check(dma_cycles==before_dma,"XP NXM cannot alias SRAM");
        // Cancelling a different controller cannot reset SERV or the active RL.
        rl_io(16'o114,256,'h190000,0);wait(dr);put_xp(4,16'o40);put(4,16'o40);
        done(0,0);bytes_at('h190000,512,0,0,"unrelated controller clear preserves RL DMA");
        // Both controllers can queue commands, receive independent completions.
        xp_io(16'o171,256,'h1a0000,2,0);rl_io(16'o114,256,'h1b0000,2);
        done(1,0);done(0,0);check(lirq,"RL is first queued interrupt");
        @(negedge clk);lack=1;@(negedge clk);lack=0;wait(xirq);
        check(xirq,"XP interrupt retained behind RL");
        @(negedge clk);xack=1;@(negedge clk);xack=0;
        bytes_at('h1a0000,512,202,0,"queued XP data");bytes_at('h1b0000,512,1,0,"queued RL data");
        // Cancel the actual owner, then immediately submit a new generation.
        xp_io(16'o171,256,'h1c0000,0,0);wait(dr && dready);put_xp(4,16'o40);
        xp_io(16'o171,256,'h1d0000,3,0);done(1,0);bytes_at('h1d0000,512,203,0,"XP generation after cancellation");
        @(negedge clk);bus_reset=1;@(negedge clk);bus_reset=0;
        check(boot_status==16'ha028,"RESET retains SD attachments");
        put_rl(2,16'o13);put_rl(0,4);done(0,0);expect_ctl(0,3,16'o235,"RL works immediately after RESET");
        check(cpu_cycles>100,"CPU continued during IO");traffic=0;
        $display("PASS MMU RL/XP: %0d checks, %0d DMA words",checks,dma_cycles);$finish;
    end
    initial begin #12000000000;$fatal(1,"global timeout PC=%h status=%h",disk.ia,boot_status);end
endmodule
