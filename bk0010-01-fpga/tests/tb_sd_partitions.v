`timescale 1ns/1ps
module tb_sd_partitions;
    reg clk = 0, reset = 1, enabled = 1, config_write = 0;
    always #5 clk = !clk;
    reg [1:0] config_address = 0, drive = 0;
    reg [15:0] config_data = 0;
    reg [10:0] offset = 0;
    reg writing = 0;
    wire [15:0] config_read;
    wire [31:0] lba;
    wire allowed, block_mode, configured;
    wire [3:0] present, readonly;
    bk_sd_partitions dut(clk,reset,enabled,config_write,config_address,config_data,
                         config_read,drive,offset,writing,allowed,lba,present,readonly,block_mode,configured);
    task put(input [1:0] address,input [15:0] value);
        begin
            @(negedge clk); config_address = address; config_data = value; config_write = 1;
            @(negedge clk); config_write = 0;
        end
    endtask
    task extent(input integer index,input [31:0] base,input [31:0] length);
        begin
            put(1,index*4); put(2,base[15:0]); put(2,base[31:16]);
            put(2,length[15:0]); put(2,length[31:16]);
        end
    endtask
    task seal;
        begin put(0,16'h8201); wait(configured); @(negedge clk); end
    endtask
    initial begin #1000000; $fatal(1,"Descriptor validation timeout"); end
    integer position;
    initial begin
        #20; reset = 0; repeat(40) @(negedge clk);
        extent(0,34816,1600); extent(1,36864,4096); extent(2,43008,1600); extent(3,0,0);
        put(1,16); put(2,2048); put(2,0); put(2,32768); put(2,0); put(2,45000); put(2,0);
        seal();
        if(!configured || present != 7 || readonly != 2 || !block_mode) $fatal(1,"Descriptors not accepted");
        for(position=0;position<1600;position=position+1) begin
            offset = position; drive = 0; writing = 1; #1;
            if(!allowed || lba != 34816+position) $fatal(1,"Raw LBA mapping");
            drive = 1; #1; if(allowed) $fatal(1,"Write protection");
            writing = 0; #1; if(!allowed || lba != 36864+position) $fatal(1,"Read-only drive read");
        end
        offset = 1600; #1; if(allowed) $fatal(1,"Geometry overflow");
        offset = 0; drive = 3; #1; if(allowed) $fatal(1,"Absent drive");
        extent(0,2048,1600); seal(); if(present[0]) $fatal(1,"FAT overwrite permitted");
        extent(0,36000,1600); seal(); if(present[0] || present[1]) $fatal(1,"Overlapping disks");
        extent(0,32'hfffffff0,1600); seal(); if(present[0]) $fatal(1,"32-bit overflow");
        extent(0,44000,1600); seal(); if(present[0]) $fatal(1,"SD capacity overflow");
        extent(0,34816,1599); seal(); if(present[0]) $fatal(1,"Short partition");
        extent(0,34816,1600); seal(); if(present != 7) $fatal(1,"Valid configuration did not recover");
        // Carry into the high half of both descriptor end and runtime LBA.
        extent(0,32'h0000fff0,1600); extent(1,32'h00010630,1600); extent(2,32'h00020000,1600);
        put(1,20); put(2,16'h1000); put(2,3); seal();
        if(present != 7) $fatal(1,"High-word bounds/carry validation");
        drive = 0; writing = 0;
        for(position=0;position<1600;position=position+1) begin
            offset = position; #1;
            if(!allowed || lba != 32'h0000fff0+position) $fatal(1,"16-bit LBA carry");
        end
        // Adjacent extents are valid; a one-sector overlap is rejected.
        extent(1,32'h0001062f,1600); seal();
        if(present[0] || present[1]) $fatal(1,"Overlap across low-word carry");
        extent(0,34816,1600); extent(1,36864,4096); extent(2,43008,1600); seal(); offset = 0;
        enabled = 0; extent(0,2048,1600); put(0,0); drive = 0; #1;
        if(!configured || !allowed || lba != 34816) $fatal(1,"Locked BIOS descriptors changed");
        $display("PASS raw SD partitions: all 1600 LBAs, RO, absent, bounds, overlap, FAT protection, lock");
        $finish;
    end
endmodule
