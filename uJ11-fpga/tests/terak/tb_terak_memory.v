`timescale 1ns/1ps
// The CPU's real MMU drives the native TERAK bus and timed external SRAM.
module tb_terak_memory #(parameter DISPLAY_EBR=1,RAM_BYTES=253952);
    reg clk=0,vclk=0,reset=1,power_on=1,peripheral_reset=0;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg request=0,writing=0,byte_access=0,physical=0;
    reg [15:0] virtual_address=0,write_data=0;
    reg [21:0] physical_address=0;
    wire ready, bus_request,bus_write,bus_byte,bus_ready,bus_error,initialized;
    wire [2:0] fault;
    wire [15:0] read_data,bus_data,bus_read_data,mmr0,mmr3;
    wire [21:0] bus_address;
    uj11_mmu mmu(.clk(clk),.reset(reset),.peripheral_reset(peripheral_reset),
        .request(request),.writing(writing),.byte_access(byte_access),.virtual_address(virtual_address),
        .write_data(write_data),.mode(2'b0),.data_space(1'b0),.physical(physical),.console(1'b0),
        .physical_address(physical_address),.instruction_start(1'b0),.instruction_pc(16'b0),
        .delta_valid(1'b0),.delta_register(3'b0),.delta_amount(5'b0),.ready(ready),.fault(fault),
        .read_data(read_data),.bus_request(bus_request),.bus_write(bus_write),.bus_byte(bus_byte),
        .bus_address(bus_address),.bus_data(bus_data),.bus_ready(bus_ready),.bus_error(bus_error),
        .bus_read_data(bus_read_data),.mmr0(mmr0),.mmr1(),.mmr2(),.mmr3(mmr3));
    wire [19:0] sa;
    wire [15:0] sd;
    wire ce,oe,we,lb,ub;
    tri1 ps2_clock;
    uj11_terak_bus #(.DISPLAY_EBR(DISPLAY_EBR),.RAM_BYTES(RAM_BYTES),.CLEAR_WORDS(RAM_BYTES/2)) bus(
        .clk(clk),.reset(reset),.power_on(power_on),.peripheral_reset(peripheral_reset),
        .video_clk(vclk),.video_reset(1'b0),.request(bus_request),.writing(bus_write),
        .byte_access(bus_byte),.cpu_lock(1'b0),.address(bus_address),.write_data(bus_data),
        .ready(bus_ready),.error(bus_error),.read_data(bus_read_data),.irq_valid(),.irq_priority(),
        .irq_vector(),.irq_ack(1'b0),.uart_rx(1'b1),.uart_tx(),.ps2_clock(ps2_clock),.ps2_data(1'b1),.tvout(),.audio(),
        .memory_initialized(initialized),.cpu_start(),.boot_complete(),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1));
    async_sram_model ram(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    reg [15:0] result;
    reg [21:0] last_pa;
    integer accesses=0;
    always @(posedge clk)if(bus_request && bus_ready)begin last_pa=bus_address;accesses++;end
    task access(input [21:0] a,input bit raw,w,b,input [15:0] value,input [2:0] expected_fault);
        @(negedge clk);physical_address=a;virtual_address=a[15:0];physical=raw;
        writing=w;byte_access=b;write_data=value;request=1;
        do @(negedge clk);while(!ready);
        if(fault!=expected_fault)$fatal(1,"memory fault at %o: got %0d expected %0d",a,fault,expected_fault);
        result=read_data;request=0;repeat(2)@(negedge clk);
    endtask
    task csr(input [21:0] a,input [15:0] value);
        access(a,1,1,0,value,0);
    endtask
    task bank(input integer n);
        csr(22'o17772344,16'(n*128)); // KIPAR2, mapping VA 040000..057777.
        csr(22'o17772304,16'o177406);
    endtask
    function automatic [15:0] pattern(input integer a);
        pattern=16'((a>>1)^(a>>10)^16'h5a5a);
    endfunction
    integer n,a,words=0;
    initial begin
        repeat(5)@(negedge clk);power_on=0;wait(initialized);@(negedge clk);reset=0;
        csr(22'o17772356,16'o7600);csr(22'o17772316,16'o177406); // Page 7 -> Q-bus I/O.
        csr(22'o17777572,1);
        if(mmr3!=0)$fatal(1,"18-bit memory test requires MMR3.22=0");
        // Fill every bank before reading any back: catches truncation and aliases.
        for(n=0;n<RAM_BYTES/8192;n++)begin
            bank(n);
            for(a=0;a<8192;a+=2)begin
                access(22'(16'o40000+a),0,1,0,pattern(n*8192+a),0);
                if(last_pa!=n*8192+a)$fatal(1,"MMU address truncated at bank %0d offset %0d",n,a);
                words++;
            end
        end
        for(n=0;n<RAM_BYTES/8192;n++)begin
            bank(n);
            for(a=0;a<8192;a+=2)begin
                access(22'(16'o40000+a),0,0,0,0,0);
                if(last_pa!=n*8192+a || result!=pattern(n*8192+a))
                    $fatal(1,"RAM alias/data mismatch bank %0d offset %0d: %h",n,a,result);
            end
        end
        // Both byte lanes in the final physical word.
        access(22'(16'o57776),0,1,1,16'h003c,0);
        access(22'(16'o57777),0,1,1,16'hc300,0);
        access(22'(16'o57776),0,0,0,0,0);
        if(result!=16'hc33c)$fatal(1,"last word byte lanes %h",result);
        // I/O aperture is distinct from RAM at the same low 16 address bits.
        access(22'o177744,0,1,0,0,0);
        access(22'o160000,0,1,1,16'h0041,0);
        access(22'o160000,0,0,1,0,0);
        if(last_pa!=22'o17760000 || result[7:0]!=8'h41)$fatal(1,"18-bit I/O page relocation");
        // Original RT-11 bootstrap detects the video buffer with a word TST.
        access(22'o160000,0,0,0,0,0);
        access(22'o160000,0,0,1,0,0);
        if(result[7:0]!=8'h41)$fatal(1,"word presence probe changed page RAM");
        if(RAM_BYTES>57344)begin
            access(22'o160000,1,0,0,0,0);
            if(result!=pattern(57344))$fatal(1,"text aperture aliases main RAM");
        end
        access(22'o177744,0,1,0,16'h0080,0);
        access(22'o160400,0,1,1,16'h0096,0);
        access(22'o177744,0,1,0,0,0);
        access(22'o160000,0,0,1,0,0);
        if(result[7:0]!=8'h41)$fatal(1,"font/page separation");
        // No hidden RAM behind Q-bus I/O and no wrap above the installed space.
        access(22'(RAM_BYTES),1,0,0,0,2);
        access(22'o1000000,1,1,0,16'habcd,2);
        access(22'(RAM_BYTES-2),1,0,0,0,0);
        if(result!=16'hc33c)$fatal(1,"NXM access changed last RAM word");
        csr(22'o17772304,16'o177402);
        access(22'o40000,0,1,0,0,3);
        csr(22'o17777572,1);
        // Guest RESET clears translation, preserving external RAM and display RAM.
        @(negedge clk);peripheral_reset=1;repeat(5)@(negedge clk);peripheral_reset=0;
        access(22'(RAM_BYTES-2),1,0,0,0,0);
        if(mmr0 || mmr3 || result!=16'hc33c)$fatal(1,"warm RESET lost RAM or retained MMU");
        access(22'o160000,0,0,1,0,0);
        if(result[7:0]!=8'h41)$fatal(1,"warm RESET lost page RAM");
        $display("PASS TERAK %0d KiB RAM: %0d words through real 18-bit MMU/SRAM, all banks, byte lanes, I/O separation, NXM and warm RESET (%0d bus accesses)",RAM_BYTES/1024,words,accesses);
        $finish;
    end
    initial begin #2000000000;$fatal(1,"memory test timeout");end
endmodule
