"""Reuse guest-level regression cases with actual HC7000 SRAM bus waveforms."""

def adapt_harness(text):
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
