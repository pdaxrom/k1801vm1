`timescale 1ns/1ps
// ROM smoke only: preloaded ROM does not replace the future SD cold-boot test.
module tb_reference_boot;
    reg clk = 0, reset = 1;
    always #5 clk = !clk;
    wire [15:0] address, data_out;
    wire [1:0] lanes;
    wire cyc, stb, we, iak, init, sys_write, sys_read, ext_write;
    reg [7:0] memory[0:65535];
    reg [7:0] kcsr = 0, sys_in = 8'o370;
    reg [7:0] key_data = 0;
    reg key_irq = 0;
    reg [15:0] shift_reg = 16'o001330;
    reg [15:0] read_data;
    wire csr_select = (address & 16'o177776) == 16'o177660 ||
        (address & 16'o177776) == 16'o177662 || (address & 16'o177776) == 16'o177664;
    wire ack = cyc && stb && (csr_select ||
        (address < 16'o177600 && (!we || address < 16'o100000)));
    always @* begin
        case (address & 16'o177776)
            16'o177660: read_data = {8'b0,kcsr};
            16'o177662: read_data = {9'b0,key_data[6:0]};
            16'o177664: read_data = shift_reg;
            default: read_data = {memory[{address[15:1],1'b0}+16'd1],memory[{address[15:1],1'b0}]};
        endcase
    end
    integer cycles = 0, screen_writes = 0, key_index = 0, key_time = 0;
    integer idle_polls = 0;
    reg booted = 0, keyboard_test = 0;
    reg [7:0] command[0:9];
    bk_cpu cpu (
        .clk_p(clk), .clk_n(!clk), .reset(reset),
        .virq(key_irq && !kcsr[6]), .stop(1'b0),
        .irq_vector(key_data[7] ? 16'o274 : 16'o60),
        .external_input(16'o177777), .system_input({8'o200,sys_in}),
        .irq_ack(iak), .peripheral_reset(init),
        .address(address), .write_data(data_out), .lanes(lanes),
        .cycle(cyc), .strobe(stb), .write(we),
        .read_data(read_data), .acknowledge(ack),
        .system_write(sys_write), .system_read(sys_read), .external_write(ext_write)
    );
    always @(posedge clk) begin
        cycles <= cycles + 1;
        if (sys_write) sys_in[2] <= 1;
        else if (sys_read) sys_in[2] <= 0;
        if (iak) key_irq <= 0;
        if (cyc && stb && ack) begin
            if (csr_select) begin
                case (address & 16'o177776)
                    16'o177660: if (we && lanes[0]) kcsr[6] <= data_out[6];
                    16'o177662: if (!we) begin kcsr[7] <= 0; sys_in[6] <= 1; key_irq <= 0; end
                    16'o177664: if (we) begin
                        if (lanes[0]) shift_reg[7:0] <= data_out[7:0];
                        if (lanes[1]) shift_reg[15:8] <= data_out[15:8] & 8'o002;
                    end
                endcase
            end else if (we) begin
                if (lanes[0]) memory[{address[15:1],1'b0}] <= data_out[7:0];
                if (lanes[1]) memory[{address[15:1],1'b0}+16'd1] <= data_out[15:8];
                if (address >= 16'o040000 && address < 16'o100000) screen_writes <= screen_writes + 1;
            end
            // The MONIT keyboard API polls its IRQ-updated RAM byte 000105.
            if (!we && address == 16'o000105 && screen_writes > 1000) begin
                idle_polls <= idle_polls + 1;
                if (!booted && idle_polls == 30) begin
                    booted <= 1; key_time <= cycles;
                    $writememh("build/rtl-boot-vram.hex",memory,16384,32767);
                    $display("PASS VM1A MONIT/BASIC boot: cycles=%0d writes=%0d",cycles,screen_writes);
                    if (!keyboard_test) $finish;
                end
                if (keyboard_test && key_index == 10 && cycles-key_time > 200000) begin
                    $writememh("build/rtl-print-vram.hex",memory,16384,32767);
                    $display("PASS VM1A keyboard command: PRINT 2+2, cycles=%0d",cycles);
                    $finish;
                end
            end
        end
        if (keyboard_test && booted && key_index < 10 && !kcsr[7] && cycles-key_time > 80000) begin
            key_data <= command[key_index]; kcsr[7] <= 1; key_irq <= 1; sys_in[6] <= 0;
            key_index <= key_index + 1; key_time <= cycles;
        end
    end
    initial begin
        keyboard_test = $test$plusargs("keyboard");
        command[0]="P"; command[1]="R"; command[2]="I"; command[3]="N"; command[4]="T";
        command[5]=" "; command[6]="2"; command[7]="+"; command[8]="2"; command[9]=8'o12;
        $readmemh("build/reference-memory.hex",memory);
        #100; reset = 0;
        #100000000;
        $fatal(1, "ROM test timeout boot=%0d keys=%0d adr=%06o ir=%06o",booted,key_index,address,cpu.core.ir);
    end
endmodule
