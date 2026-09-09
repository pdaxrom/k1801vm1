`timescale 1ns/1ps
// Cold board boot: no register writes or injected CPU state. SD image is read-only.
module tb_board_rt11;
    reg clk=0, reset=1, rx=1;
    wire tx,fc,fs,fm,fi,sc,ss,sm,si,boot_complete,stopped;
    integer clocks=0,rk_commands=0,timer_edges=0,stage=0,prompt_count=0;
    integer retirements=0,read_beats=0,write_beats=0;
    reg previous_boot=0,previous_event=0;
    reg [39:0] window=0;
    reg [7:0] previous_char=0;
    reg banner=0, directory_seen=0;
    reg trace_rk=0, finish_pending=0;
    reg [7:0] expected_tx[0:8191];
    integer bus_chars=0,serial_chars=0,uart_file,bit_index;
    reg [7:0] serial_value;
    string uart_path;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    // Check the actual pin waveform, including every stop bit. The CSR
    // transcript alone would miss a dropped holding-register byte.
    initial begin
        if(!$value$plusargs("UART_LOG=%s",uart_path))uart_path="build/cp28-uart.txt";
        uart_file=$fopen(uart_path,"w");
        if(uart_file==0)$fatal(1,"UART transcript open failed");
        wait(!reset);
        forever begin
            @(negedge tx);repeat(128)@(negedge clk);
            if(tx!==0)$fatal(1,"UART false start");
            for(bit_index=0;bit_index<8;bit_index=bit_index+1)begin
                repeat(257)@(negedge clk);serial_value[bit_index]=tx;
            end
            repeat(257)@(negedge clk);
            if(tx!==1)$fatal(1,"UART stop bit missing");
            if(serial_chars>=bus_chars || serial_value!==expected_tx[serial_chars])
                $fatal(1,"UART wire mismatch byte%0d got%h",serial_chars,serial_value);
            serial_chars=serial_chars+1;
            $fwrite(uart_file,"%c",serial_value);$fflush(uart_file);
        end
    end
    always #5 clk=~clk;
    uj11_board dut(.clk(clk),.reset(reset),.uart_rx(rx),.uart_tx(tx),.panel_keys(4'b0),
        .panel_din(),.panel_ce(),.panel_clk(),.panel_rs(),.panel_blank(),.panel_latch(),
        .host_miso(),.host_miso_oe(),.fram_cs_n(fc),.fram_sck(fs),.fram_mosi(fm),.fram_miso(fi),
        .sd_cs_n(sc),.sd_sck(ss),.sd_mosi(sm),.sd_miso(si),.boot_complete(boot_complete),.stopped(stopped));
    spi_fram_model fram(.cs_n(fc),.sck(fs),.mosi(fm),.miso(fi));
    spi_sd_model card(.cs_n(sc),.sck(ss),.mosi(sm),.miso(si),.absent(1'b0),
        .fail_read(1'b0),.fail_write(1'b0),.stuck_busy(1'b0),.bad_ocr(1'b0),.bad_echo(1'b0),.bad_status(1'b0));
    task send_byte(input [7:0] value);
        integer b;
        begin
            @(negedge clk);rx=0;repeat(257)@(negedge clk);
            for(b=0;b<8;b=b+1)begin rx=value[b];repeat(257)@(negedge clk);end
            rx=1;repeat(100000)@(negedge clk);
        end
    endtask
    initial begin
        trace_rk=$test$plusargs("TRACE_RK");
        repeat(15)@(negedge clk);reset=0;
        // This image runs two commands in STARTF.COM; third prompt is interactive.
        wait(prompt_count==3);
        repeat(10000)@(negedge clk);
        send_byte("D");send_byte("I");send_byte("R");send_byte(13);
        stage=1;
    end
    always @(posedge clk) if(!reset)begin
        clocks<=clocks+1;
        if(dut.cpu.retire)retirements<=retirements+1;
        if(dut.acknowledge)begin
            if(dut.writing)write_beats<=write_beats+1;else read_beats<=read_beats+1;
        end
        previous_boot<=boot_complete; previous_event<=dut.event_irq;
        if(dut.event_irq && !previous_event)timer_edges<=timer_edges+1;
        if(boot_complete && !previous_boot)$display("uJ11 SD handoff at %0d clocks",clocks);
        if(stopped)$fatal(1,"board stopped PC%o IR%o uPC%h fault%0d",dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir,dut.cpu.engine.debug_upc,dut.cpu.engine.fault_code);
        if(dut.acknowledge && dut.error)$display("BUS FAULT addr%o wr%b PC%o SP%o IR%o uPC%h PSW%o",
            dut.address,dut.writing,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.dp.rf.words[6],dut.cpu.engine.ir,dut.cpu.engine.debug_upc,dut.cpu.engine.psw);
        if($test$plusargs("TRACE_BOOT") && dut.acknowledge && dut.opcode_fetch && dut.address>=16'o001000 && dut.address<16'o001056)
            $display("BOOT fetch %o:%o SP%o R0%o R1%o PSW%o",dut.address,dut.rdata,dut.cpu.engine.dp.rf.words[6],
                dut.cpu.engine.dp.rf.words[0],dut.cpu.engine.dp.rf.words[1],dut.cpu.engine.psw);
        if(dut.acknowledge && dut.writing && dut.address==16'o177440)begin
            rk_commands<=rk_commands+1;
            if(trace_rk)$display("RK command %o PC%o IPL%0d",dut.data,dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.psw[7:5]);
        end
        if(dut.acknowledge && dut.writing && dut.address==16'o177566)begin
            if(bus_chars>=8192)$fatal(1,"UART scoreboard capacity");
            expected_tx[bus_chars]=dut.data[7:0];bus_chars=bus_chars+1;
            window<={window[31:0],dut.data[7:0]};previous_char<=dut.data[7:0];
            $write("%c",dut.data[7:0]);
            if({window[31:0],dut.data[7:0]}=="RT-11")banner<=1;
            if({window[31:0],dut.data[7:0]}=="Files")directory_seen<=1;
            if(banner && previous_char==10 && dut.data[7:0]==".")begin
                prompt_count<=prompt_count+1;
                if(stage==1)begin
                    if(!directory_seen || timer_edges==0 || !dut.bus.timer_ie || rk_commands<2 || !boot_complete)
                        $fatal(1,"RT-11 DIR incomplete/timer missing: directory%0d edges%0d IE%0d",directory_seen,timer_edges,dut.bus.timer_ie);
                    finish_pending<=1;stage=2;
                end
            end
        end
        if(finish_pending && serial_chars==bus_chars)begin
            if(card.writes==0)$fatal(1,"RT-11 did not exercise SD writeback");
            $fclose(uart_file);
            $display("\nPASS uJ11 cold RT-11 boot + DIR: %0d clocks, %0d RK commands, %0d timer edges, %0d UART wire bytes, %0d SD reads/%0d writes",clocks,rk_commands,timer_edges,serial_chars,card.read_count,card.writes);
            $display("BOARD COUNTS retired%0d reads%0d writes%0d FRAM-CS%0d",retirements,read_beats,write_beats,fram.transaction_count);
            $finish;
        end
        if(trace_rk && clocks!=0 && clocks%5000000==0)begin
            $display("BOARD clocks%0d PC%o IR%o uPC%h PSW%o cmds%0d reads%0d service%b pending%b",clocks,
                dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir,dut.cpu.engine.debug_upc,dut.cpu.engine.psw,
                rk_commands,card.read_count,dut.bus.rk_service_active,dut.bus.rk_service_pending);
            $fflush();
        end
        if(clocks>=500000000)$fatal(1,"RT-11 board timeout PC%o IR%o",dut.cpu.engine.dp.rf.words[7],dut.cpu.engine.ir);
    end
endmodule
