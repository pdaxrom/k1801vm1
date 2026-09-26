`timescale 1ns/1ps
// Full CPU, FRAM, SD and UART waveforms; no CPU state or peripheral-bus forcing.
module tb_rt11;
    reg clk=0,reset=1,rx=1,halt_button=0;
    wire tx,fc,fs,fm,fi,sc,ss,sm,si,boot_complete,stopped;
    wire din,ce,sck,rs,blank,latch;
    reg [7:0] shifted=0,outputs=0;
    reg [3:0] keys=0;
    integer keycode=-1,display_count=0,display_frames=0;
    reg [639:0] display_bits=0,last_display=0;
    reg [7:0] font[0:319];
    reg [7:0] corrupt_saved;
    integer clocks=0,prompts=0,loader_prompts=0,odt_prompts=0,phase=0;
    integer bus_chars=0,serial_chars=0,uart_file,b,checks=0,oldprompt=0,oldodt=0;
    integer guest_fetches=0,oldfetches=0,rx_overruns=0;
    reg [7:0] expected_tx[0:65535];
    reg [7:0] serial_value,previous_char=0;
    reg [63:0] window=0;
    string uart_path,segment="",cmdtext;
    `include "odt_symbols.vh"
    always #5 clk=~clk;
    // Same matrix electrical ordering as PNLDRV, not direct CPU key injection.
    always @*begin
        keys=0;
        case(keycode)
        0:if(outputs[3])keys=1; 1:if(outputs[3])keys=2;4:if(outputs[3])keys=4;7:if(outputs[3])keys=8;
        10:if(outputs[4])keys=1;2:if(outputs[4])keys=2;5:if(outputs[4])keys=4;8:if(outputs[4])keys=8;
        11:if(outputs[5])keys=1;3:if(outputs[5])keys=2;6:if(outputs[5])keys=4;9:if(outputs[5])keys=8;
        15:if(outputs[6])keys=1;14:if(outputs[6])keys=2;13:if(outputs[6])keys=4;12:if(outputs[6])keys=8;
        16:if(outputs[7])keys=1;17:if(outputs[7])keys=2;18:if(outputs[7])keys=4;19:if(outputs[7])keys=8;
        default:keys=0;
        endcase
    end
    uj11_board dut(.clk(clk),.reset(reset),.halt_button(halt_button),.uart_rx(rx),.uart_tx(tx),.panel_keys(keys),
        .panel_din(din),.panel_ce(ce),.panel_clk(sck),.panel_rs(rs),.panel_blank(blank),.panel_latch(latch),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(sc),.sd_sck(ss),.sd_mosi(sm),.sd_miso(si),.boot_complete(boot_complete),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
    spi_sd_model card(.cs_n(sc),.sck(ss),.mosi(sm),.miso(si),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    always @(posedge sck)begin
        shifted={shifted[6:0],din};
        if(!ce)begin display_bits={display_bits[638:0],din};display_count=display_count+1;end
    end
    always @(posedge latch)outputs=shifted;
    always @(negedge ce)begin display_count=0;display_bits=0;end
    // Observe the physical command stream, not a CPU register/label value.
    // Eight repeated CW0 bytes cover the existing HCMS daisy-chain transfer.
    always @(posedge ce)if(display_count!=0 && rs)begin
        if(display_count!=64 || display_bits[63:0]!=={8{8'h6c}})
            $fatal(1,"bad HCMS brightness command: %0d bits %016h",display_count,display_bits[63:0]);
    end
    always @(posedge ce)if(display_count!=0 && !rs)begin
        if(display_count!=640)$fatal(1,"bad HDSP frame: %0d bits",display_count);
        display_frames=display_frames+1;last_display=display_bits;
    end
    function [15:0] upper(input integer a);return {fram.memory[65536+a+1],fram.memory[65536+a]};endfunction
    task check(input bit ok,input string why);
        begin if(!ok)$fatal(1,"phase%0d %s",phase,why);checks=checks+1;end
    endtask
    task contains(input string expected);
        integer j;reg found;
        begin
            found=0;
            for(j=0;j+expected.len()<=segment.len();j=j+1)if(segment.substr(j,j+expected.len()-1)==expected)found=1;
            if(!found)$fatal(1,"phase%0d missing [%s] in [%s]",phase,expected,segment);checks=checks+1;
        end
    endtask
    initial begin
        if(!$value$plusargs("UART_LOG=%s",uart_path))$fatal(1,"UART_LOG required");
        uart_file=$fopen(uart_path,"w");wait(!reset);
        forever begin
            @(negedge tx);repeat(128)@(negedge clk);
            if(tx!==0)$fatal(1,"UART false start");
            for(b=0;b<8;b=b+1)begin repeat(257)@(negedge clk);serial_value[b]=tx;end
            repeat(257)@(negedge clk);
            if(tx!==1 || serial_chars>=bus_chars || serial_value!==expected_tx[serial_chars])$fatal(1,"UART waveform mismatch byte%0d",serial_chars);
            serial_chars=serial_chars+1;$fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    task check_display(input string text16);
        reg [639:0] golden;
        integer i,j,k,c;
        begin
            golden=0;check(text16.len()==16,"display expectation length");
            for(i=0;i<16;i=i+1)begin
                k=(i+8)%16;c=int'(text16[k]);
                for(j=0;j<5;j=j+1)golden={golden[631:0],font[(c-32)*5+j]};
                check(fram.memory[65536+O_SCREEN+i]==text16[i],"HDSP window text");
            end
            check(last_display==golden,"physical 640-bit glyph frame and module order");
        end
    endtask
    task send_byte(input [7:0] value);
        integer bitno;
        begin
            @(negedge clk);rx=0;repeat(257)@(negedge clk);
            for(bitno=0;bitno<8;bitno=bitno+1)begin rx=value[bitno];repeat(257)@(negedge clk);end
            rx=1;repeat(50000)@(negedge clk);
        end
    endtask
    task send_line(input string value);
        integer j;
        begin for(j=0;j<value.len();j=j+1)send_byte(value[j]);send_byte(13);end
    endtask
    task shell(input string value);
        begin oldprompt=prompts;segment="";send_line(value);wait(prompts>oldprompt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);end
    endtask
    task odt_command(input string value);
        begin oldodt=odt_prompts;segment="";send_line(value);wait(odt_prompts>oldodt);wait(serial_chars==bus_chars);repeat(10000)@(negedge clk);end
    endtask
    task keypress(input integer key);
        begin
            $display("KEY start %0d clocks%0d last%o",key,clocks,upper(O_KLAST));$fflush();
            while(upper(O_KLAST)!=65535)@(negedge clk);@(negedge clk);keycode=key;
            while(upper(O_KLAST)!=16'(key))@(negedge clk);repeat(10000)@(negedge clk);keycode=-1;
            while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
            $display("KEY done %0d clocks%0d last%o",key,clocks,upper(O_KLAST));$fflush();
        end
    endtask
    always @(posedge clk)if(!reset)begin
        clocks<=clocks+1;
        if(dut.acknowledge && dut.opcode_fetch && !dut.bank)guest_fetches<=guest_fetches+1;
        if(phase>=4 && dut.bus.fixed_uart.console.rx_busy && dut.bus.fixed_uart.console.rx_bits==9 && dut.bus.fixed_uart.console.rx_timer==0 && dut.bus.fixed_uart.console.rx_full)rx_overruns<=rx_overruns+1;
        if(dut.acknowledge && dut.writing && dut.bus.uart_selected && dut.address==16'o177566)begin
            expected_tx[bus_chars]=dut.data[7:0];bus_chars=bus_chars+1;segment={segment,dut.data[7:0]};
            window={window[55:0],dut.data[7:0]};
            if(previous_char==10 && dut.data[7:0]==".")prompts<=prompts+1;
            if(window=="UJLOAD> ")loader_prompts<=loader_prompts+1;
            if(window[39:0]=="ODT> ")odt_prompts<=odt_prompts+1;
            previous_char<=dut.data[7:0];
        end
        if(stopped)$fatal(1,"stopped phase%0d PC%o IR%o",phase,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir);
        if(clocks!=0 && clocks%20000000==0)begin $display("CP64 progress %0d phase%0d PC%o UART%0d",clocks,phase,dut.cpu.engine.dp.rf.words[7],bus_chars);$fflush();end
        if(clocks>500000000)$fatal(1,"timeout phase%0d PC%o",phase,dut.cpu.engine.dp.rf.words[7]);
    end
