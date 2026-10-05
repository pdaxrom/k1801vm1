`timescale 1ns/1ps
module tb_video;
`include "rtl/bk_rgb_index.vh"
`include "rtl/bk_palettes.vh"
    reg clk = 0, video_clk = 0, power_on = 1;
    always #20.833333 clk = !clk;
    always #7.8125 video_clk = !video_clk;
    reg [15:0] scroll = 16'o001330;
    reg model11=0,page=0;
    reg [3:0] palette=0;
    reg stalled = 0;
    wire [1:0] initialized, underrun;
    function [15:0] pattern(input integer address);
        begin pattern = (address * 16'h93b7) ^ (address >> 3) ^ 16'ha35c; end
    endfunction
    function [5:0] mono_dac(input integer x, row, input [15:0] view_config);
        integer physical_row, address, pixel;
        reg [15:0] word_data;
        begin
            physical_row = (row + view_config[7:0] - 8'o330) & 255;
            address = view_config[9] ? 8192 + physical_row*32 : 14336 + (physical_row & 63)*32;
            pixel = ((x - 718)*512)/3200;
            word_data = pattern(address + pixel/16);
            mono_dac = word_data[pixel & 15] ? 6'd50 : 6'd15;
        end
    endfunction
    genvar mode;
    generate for(mode=0;mode<2;mode=mode+1) begin: modes
        wire reset = power_on || !initialized[mode];
        wire video_request, video_ready, request, writing, mem_ready, cpu_ready;
        wire [19:0] video_address, address, pins_address;
        wire [15:0] data, result, pins_data;
        wire [1:0] lanes;
        wire [5:0] dac;
        wire ce, oe, we, lb, ub;
        reg cpu_request = 0;
        integer cpu_count = 0, samples = 0, fields = 0, pixels;
        integer physical_row, word_address, expected_bits, sample_pixel, position;
        integer left_edges = 0, right_edges = 0, top_edges = 0, bottom_edges = 0;
        reg expected_active;
        reg [4:0] dac_check = 0;
        reg [5:0] expected_dac[0:4];
        integer delay_index;
        reg [7:0] expected_rgb;
        reg [15:0] expected_word;
        wire [5:0] reference_dac;
        integer valid_cycles=0,black_samples=0,page5_samples=0,page6_samples=0;
        bk_pal_video #(.COLOR(mode)) video(clk,video_clk,reset,scroll,video_request,video_address,
                                          video_ready,result,dac,underrun[mode],model11,mode != 0,page,palette);
        uj11_pal_encoder reference_encoder(video_clk,video.vrst,video.sync,video.burst,
            expected_active && video.display_valid && (!video.vmodel11 || video.vscroll[9] || video.row<64),
            video.alternate,expected_rgb,reference_dac);
        bk_sram_arbiter arbiter(clk,reset,cpu_request,1'b0,20'd0,16'b0,2'b11,cpu_ready,
                               video_request,video_address,video_ready,request,writing,address,data,lanes,mem_ready);
        uj11_sram #(.CLEAR_WORDS(16)) controller(clk,power_on,1'b0,request && !stalled,writing,address,
                                                lanes,data,result,mem_ready,initialized[mode],
                                                pins_address,pins_data,ce,oe,we,lb,ub);
        sram_model ram(pins_address,pins_data,ce,oe,we,lb,ub);
        integer i;
        initial for(i=0;i<131072;i=i+1) ram.memory[i] = pattern(i);
        // Saturate CPU requests while PAL fetches: every grant competes.
        always @(posedge clk) begin
            if(reset) begin cpu_request <= 0; cpu_count <= 0; end
            else if(cpu_ready) begin cpu_request <= 0; cpu_count <= cpu_count + 1; end
            else if(!cpu_request && arbiter.owner == 0) cpu_request <= 1;
        end
        // Independent framebuffer-to-DAC reference across all five encoder
        // stages, including the first/last pixel and first/last visible row.
        always @(posedge video_clk) begin
            if(reset || video.vrst || stalled) dac_check <= 0;
            else dac_check <= {dac_check[3:0], mode == 0 && !video.vmodel11 && video.active && video.display_valid};
            if(mode == 0 && video.active) expected_dac[0] <= mono_dac(video.x,video.row,video.vscroll);
            for(delay_index=1;delay_index<5;delay_index=delay_index+1)
                expected_dac[delay_index] <= expected_dac[delay_index-1];
        end
        always @(negedge video_clk) if(!reset) begin
            if(video.vrst) valid_cycles=0;else valid_cycles=valid_cycles+1;
            if(valid_cycles>10 && !stalled && dac!==reference_dac)
                $fatal(1,"PAL reference DAC mode=%0d model11=%0d palette=%0d got=%0d want=%0d",mode,
                       video.vmodel11,video.vpalette,dac,reference_dac);
            if(dac_check[4] && dac !== expected_dac[4])
                $fatal(1,"PAL DAC pixel/latency got=%0d expected=%0d",dac,expected_dac[4]);
            expected_active = ((video.line_number >= 32 && video.line_number < 288) ||
                               (video.line_number >= 345 && video.line_number < 601)) &&
                               video.x >= 718 && video.x < 3918;
            if(video.active !== expected_active) $fatal(1,"PAL viewport mode=%0d line=%0d x=%0d",mode,
                                                       video.line_number,video.x);
            if(!video.vrst && video.x[10:0] == 0 && (video.half_line == 0 || video.half_line == 625))
                fields = fields + 1;
            if(video.active && !stalled) begin
                if(!video.display_valid) $fatal(1,"Missing PAL line mode=%0d row=%0d",mode,video.row);
                physical_row = (video.row + video.vscroll[7:0] - 8'o330) & 255;
                word_address = video.vmodel11 ? (video.vpage ? 49152 : 40960) : 8192;
                if(video.vscroll[9]) word_address = word_address + physical_row*32;
                else word_address = word_address + 6144 + (physical_row & 63)*32;
                // Independent horizontal reference, including each word boundary.
                position = video.x - 718;
                sample_pixel = (position * (mode ? 256 : 512)) / 3200;
                expected_bits = sample_pixel * (mode ? 2 : 1);
                expected_word = pattern(word_address + expected_bits/16);
                expected_word = expected_word >> (expected_bits & 15);
                if(!mode) expected_rgb = expected_word[0] ? 8'hff : 0;
                else expected_rgb = bk_palette_rgb(video.vmodel11 ? video.vpalette : 4'b0,expected_word[1:0]);
                if(video.color_index !== bk_rgb_index(expected_rgb))
                    $fatal(1,"PAL pixels mode=%0d row=%0d x=%0d bit=%0d got=%h want=%h",mode,
                           video.row,video.x,expected_bits,video.color_index,bk_rgb_index(expected_rgb));
                samples = samples + 1;
                if(video.vmodel11) begin
                    if(video.vpage)page6_samples=page6_samples+1;else page5_samples=page5_samples+1;
                    if(!video.vscroll[9] && video.row>=64)black_samples=black_samples+1;
                end
                if(video.x == 718) begin
                    left_edges = left_edges + 1;
                    if(video.row == 0) top_edges = top_edges + 1;
                    if(video.row == 255) bottom_edges = bottom_edges + 1;
                end
                if(video.x == 3917) right_edges = right_edges + 1;
            end
        end
    end endgenerate
    integer palette_index,previous_fields;
    initial begin
        #300; power_on = 0;
        wait(&initialized); scroll = 16'o001335;
        wait(modes[0].fields == 3); #100000; scroll = 16'o000335;
        wait(modes[0].fields == 5); #100000;
        if(|underrun || modes[0].samples < 3000000 || modes[1].samples < 3000000)
            $fatal(1,"PAL starvation/sample count");
        if(modes[0].video.vscroll != 16'o000335 || modes[1].video.vscroll != 16'o000335)
            $fatal(1,"RP/scroll mailbox not applied");
        if(modes[0].left_edges < 1024 || modes[0].right_edges < 1024 ||
           modes[0].top_edges < 4 || modes[0].bottom_edges < 4 ||
           modes[1].left_edges < 1024 || modes[1].right_edges < 1024 ||
           modes[1].top_edges < 4 || modes[1].bottom_edges < 4)
            $fatal(1,"PAL framebuffer edges missing");
        model11=1;scroll=16'o001335;
        for(palette_index=0;palette_index<16;palette_index=palette_index+1)begin
            palette=palette_index;page=palette_index>=8;
            wait(modes[1].video.vmodel11 && modes[1].video.vpalette==palette && modes[1].video.vpage==page);
            previous_fields=modes[0].fields;
            wait(modes[0].fields>=previous_fields+2);
        end
        scroll=16'o000335;wait(!modes[1].video.vscroll[9]);
        previous_fields=modes[0].fields;wait(modes[0].fields>=previous_fields+2);
        if(|underrun || modes[0].page5_samples<1000000 || modes[0].page6_samples<1000000 ||
           modes[1].page5_samples<1000000 || modes[1].page6_samples<1000000 ||
           modes[0].black_samples<100000 || modes[1].black_samples<100000)
            $fatal(1,"BK11 page/palette/RP coverage");
        stalled = 1;
        wait(&underrun);
        $display("PASS PAL mono/colour: %0d/%0d samples, all 16 BK11 palettes, pages 5/6, both RP modes, exact DAC, CPU contention, underrun",modes[0].samples,modes[1].samples);
        $finish;
    end
    initial begin #1500000000; $fatal(1,"PAL timeout"); end
endmodule
