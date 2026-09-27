"""Reuse guest-level regression cases with actual HC7000 SRAM bus waveforms."""

def adapt_harness(text):
    text=text.replace('module tb_rt11;', '''module tb_rt11;
    integer parallel_fetches=0,dma_words=0;
    always @(posedge clk) if(!reset && initialized) begin
        if(dut.bus.disk.rk_busy && dut.acknowledge && dut.opcode_fetch)parallel_fetches<=parallel_fetches+1;
        if(dut.bus.dma_request && dut.bus.dma_ready)dma_words<=dma_words+1;
        if(dut.device_irq && dut.device_vector==16'o160000)
            $fatal(1,"HC7000 entered the old CPU disk service");
    end''')
    text=text.replace('$display("PASS CP79 RT11 modules:', '''check(parallel_fetches>100,"CPU executes instructions during disk commands");
        check(dma_words>100,"RT-11 used autonomous SRAM DMA");
        $display("HC7000 concurrency: %0d instruction fetches during RK busy, %0d DMA words",parallel_fetches,dma_words);
        $display("PASS CP79 RT11 modules:''')
    text=text.replace('always #5 clk=~clk;', 'always #20.833 clk=~clk;')
    text=text.replace('    uj11_board dut(', '''    reg power_on=1;
    initial begin repeat(5)@(negedge clk);power_on=0;end
    wire initialized;
    wire [19:0] sram_address;
    wire [15:0] sram_data;
    wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n;
    uj11_hc7000_board dut(.power_on(power_on),.memory_initialized(initialized),''')
    text=text.replace('.reset(reset),', '.reset(reset || !initialized),')
    text=text.replace('.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),',
        '''.sram_address(sram_address),.sram_data(sram_data),
        .sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),.sram_we_n(sram_we_n),
        .sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n),''')
    text=text.replace('spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));',
        '''async_sram_model fram(.address(sram_address),.data(sram_data),
        .ce_n(sram_ce_n),.oe_n(sram_oe_n),.we_n(sram_we_n),
        .lb_n(sram_lb_n),.ub_n(sram_ub_n));''')
    text=text.replace('repeat(128)', 'repeat(104)').replace('repeat(257)', 'repeat(208)')
    return text
