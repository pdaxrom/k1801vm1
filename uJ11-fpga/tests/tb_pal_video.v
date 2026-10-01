`timescale 1ns/1ps
module tb_pal_video;
`ifdef UJ11_VIDEO_VENDOR_RAM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0,vclk=0,reset=1,power_on=1;
    always #10 clk=~clk;
    always #7.8125 vclk=~vclk;
    reg rw=0;reg [2:0] ra=0;reg [15:0] wd=0;reg [1:0] lanes=3;
    wire [15:0] rd;
    wire vr,vready;wire [19:0] va;wire [5:0] dac;
    uj11_pal_video dut(.clk(clk),.video_clk(vclk),.reset(reset),.reg_write(rw),
        .reg_address(ra),.reg_data(wd),.reg_lanes(lanes),.reg_read(rd),
        .dma_request(vr),.dma_address(va),.dma_ready(vready),.dma_data(memdata),.dac(dac));
    reg cpu_request=0,disk_request=0,cpu_lock=0,traffic=0;
    wire cpu_ready,disk_ready;
    wire mr,mw,mready;wire [19:0] ma;wire [1:0] ml;wire [15:0] md,memdata;
    uj11_video_arbiter #(.FAST_TURNAROUND(1)) arb(.clk(clk),.reset(reset),
        .cpu_request(cpu_request),.cpu_write(1'b0),.cpu_lock(cpu_lock),.cpu_address(20'h1000),
        .cpu_lanes(2'b11),.cpu_data(16'b0),.cpu_ready(cpu_ready),
        .dma_request(disk_request),.dma_lanes(2'b11),.dma_write(1'b0),.dma_address(22'h4000),.dma_data(16'b0),.dma_ready(disk_ready),
        .video_request(vr),.video_address(va),.video_ready(vready),
        .request(mr),.write(mw),.address(ma),.lanes(ml),.data(md),.ready(mready));
    wire initialized;wire [19:0] sa;wire [15:0] sd;wire ce,oe,we,lb,ub;
    uj11_sram #(.CLEAR_WORDS(1),.FAST_RESPONSE(1)) memory(.clk(clk),.power_on(power_on),.reset(reset),
        .initialized(initialized),.request(mr),.write(mw),.address(ma),.byte_enable(ml),
        .write_data(md),.read_data(memdata),.ready(mready),.sram_address(sa),.sram_data(sd),
        .sram_ce_n(ce),.sram_oe_n(oe),.sram_we_n(we),.sram_lb_n(lb),.sram_ub_n(ub));
    async_sram_model chip(.address(sa),.data(sd),.ce_n(ce),.oe_n(oe),.we_n(we),.lb_n(lb),.ub_n(ub));
    integer cpus=0,disks=0,dmas=0,checked=0,frames=0,lines=0,bursts=0,broads=0,equals=0,normals=0;
    reg allow_underrun=0;
    reg [255:0] pattern_colours=0;
    reg [7:0] palette[0:255];
    function [7:0] pattern;
        input integer a;
        begin pattern=(a^(a>>8)^(a>>16))&255;end
    endfunction
    always @(posedge clk) begin
        if(reset || !traffic)begin cpu_request<=0;disk_request<=0;end
        else begin
            if(cpu_ready)begin cpu_request<=0;cpus<=cpus+1;end
            else if(!cpu_request)cpu_request<=1;
            if(disk_ready)begin disk_request<=0;disks<=disks+1;end
            else if(!disk_request)disk_request<=1;
        end
        if(!reset && vready)begin
            if(va<20'hf0000 || va>=20'hffd00)$fatal(1,"Video escaped reserved test RAM: %h",va);
            if(memdata!=={pattern({va,1'b1}),pattern({va,1'b0})})$fatal(1,"SRAM data/order");
            dmas<=dmas+1;
        end
    end
    realtime fill_started,max_fill=0;
    reg [2:0] old_state=0;
    always @(posedge clk)begin
        old_state<=dut.state;
        if(dut.state==1 && old_state==0)fill_started=$realtime;
        if(dut.state==4 && old_state!=4 && !allow_underrun)begin
            if($realtime-fill_started>max_fill)max_fill=$realtime-fill_started;
            if($realtime-fill_started>50000)$fatal(1,"Line deadline %.1f ns",$realtime-fill_started);
        end
    end
    integer pos,yy,addr,idx,n_sync=0,n_burst=0;
    reg prev_sync=0,prev_burst=0;
    integer ticks=0,last_frame=-1,last_line=-1,complete_frames=0,frames_before_reset=0;
    always @(posedge vclk)if(dut.vrst)begin
        last_frame=-1;last_line=-1;prev_sync=0;prev_burst=0;n_sync=0;n_burst=0;
        broads=0;equals=0;normals=0;bursts=0;complete_frames=0;
    end else begin
        ticks=ticks+1;
        if(dut.line_start)begin
            if(last_line>=0 && ticks-last_line!=4096)$fatal(1,"H period");
            last_line=ticks;lines=lines+1;
        end
        if(dut.frame_start)begin
            frames=frames+1;
            if(last_frame>=0)begin
                if(ticks-last_frame!=2560000)$fatal(1,"625-line frame period");
                if(broads!=10 || equals!=20 || normals!=610 || bursts!=607)
                    $fatal(1,"PAL pulses broad=%0d equal=%0d normal=%0d bursts=%0d",broads,equals,normals,bursts);
                complete_frames=complete_frames+1;
            end
            last_frame=ticks;broads=0;equals=0;normals=0;bursts=0;
        end
        if(dut.sync)n_sync=n_sync+1;
        if(prev_sync && !dut.sync)begin
            case(n_sync)
                150: equals=equals+1;
                301: normals=normals+1;
                1747: broads=broads+1;
                default: $fatal(1,"Invalid sync pulse %0d",n_sync);
            endcase
            n_sync=0;
        end
        if(dut.burst)n_burst=n_burst+1;
        if(prev_burst && !dut.burst)begin
            if(n_burst!=144)$fatal(1,"PAL burst duration");
            bursts=bursts+1;n_burst=0;
        end
        prev_sync=dut.sync;prev_burst=dut.burst;
        if(dut.active && dut.vcontrol==5)pattern_colours[dut.rgb]=1;
        if(dut.active && dut.vcontrol[0] && !dut.vcontrol[2])begin
            if(!dut.display_valid)begin
                if(!allow_underrun)$fatal(1,"Unexpected underrun line=%0d row=%0d",dut.line_number,dut.row);
                if(dut.rgb!==0)$fatal(1,"Underrun must blank complete line");
            end else begin
                pos=(dut.x-768)/(dut.vcontrol[1] ? 10 : 5);
                yy=(dut.row+dut.vorigin)%200;
                addr=2*dut.active_base+320*yy+(dut.vcontrol[1] ? pos : pos/2);
                idx=pattern(addr);
                if(!dut.vcontrol[1])idx=(pos&1) ? idx>>4 : idx&15;
                if(dut.rgb!==palette[idx])$fatal(1,"Pixel row=%0d pos=%0d addr=%h got=%h expected=%h",dut.row,pos,addr,dut.rgb,palette[idx]);
                checked=checked+1;
            end
        end
    end
    task write_reg(input [2:0] a,input [15:0] v);
        begin @(negedge clk);ra=a;wd=v;rw=1;@(negedge clk);rw=0;end
    endtask
    task commit;
        begin write_reg(4,1);wait(dut.config_busy);wait(!dut.config_busy);repeat(5)@(posedge clk);end
    endtask
    initial begin
        for(integer i=0;i<2097152;i=i+1)chip.memory[i]=pattern(i);
        for(integer i=0;i<256;i=i+1)palette[i]=i;
        repeat(8)@(negedge clk);power_on=0;
        wait(initialized);repeat(8)@(negedge clk);reset=0;
        wait(!dut.palette_init);traffic=1;
        repeat(300)@(posedge clk);if(vr || dmas)$fatal(1,"Video on at reset");
        write_reg(1,0);write_reg(2,16'h1e);write_reg(0,1);commit();
        wait(complete_frames>=2);
        write_reg(2,16'h1f);write_reg(3,193);write_reg(0,3);commit();
        wait(complete_frames>=4);
        write_reg(5,7);write_reg(6,255); // Must be rejected while display active.
        wait(complete_frames>=5);
        write_reg(0,0);commit();write_reg(5,7);write_reg(6,255);palette[7]=255;
        write_reg(0,1);commit();wait(complete_frames>=8);
        write_reg(1,16'hffff);write_reg(2,31);write_reg(3,200);write_reg(4,1);
        if(!dut.bad_config || dut.config_busy)$fatal(1,"Invalid config accepted");
        allow_underrun=1;wait(dut.x==32 && dut.row==70);cpu_lock=1;
        #180000;cpu_lock=0;
        wait(dut.x==0 && dut.row==90);allow_underrun=0;
        if(!dut.underruns)$fatal(1,"Lost underrun status");
        write_reg(7,1);repeat(20)@(posedge clk);
        if(dut.underruns || dut.bad_config)$fatal(1,"Status clear");
        wait(complete_frames>=9);
        if(cpus<100 || disks<100 || checked<1000000)$fatal(1,"Missing contention/coverage");
        frames_before_reset=complete_frames;
        // Reset/PLL-loss assertion while a real SRAM read is outstanding.
        wait(vr && dut.active_line);@(negedge clk);reset=1;
        repeat(20)@(negedge clk);
        if(vr || dut.vcontrol || dut.config_busy)$fatal(1,"Reset did not abort video");
        reset=0;wait(!dut.palette_init);
        for(integer i=0;i<256;i++)palette[i]=i;
        write_reg(1,0);write_reg(2,16'h1e);write_reg(0,3);commit();
        wait(complete_frames>=2);
        if(dut.underruns)$fatal(1,"Reset recovery underrun");
        write_reg(0,5);commit();wait(complete_frames>=4);
        if(!(&pattern_colours))$fatal(1,"Built-in colour grid missed RGB332 codes");
        $display("PASS PAL framebuffer: %0d frames %0d pixels, CPU=%0d disk=%0d video=%0d, max line %.1f ns",complete_frames+frames_before_reset,checked,cpus,disks,dmas,max_fill);
        $finish;
    end
    initial begin #650000000;$fatal(1,"PAL test timeout");end
endmodule
