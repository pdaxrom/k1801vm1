`timescale 1ns/1ps
// Preloaded native ROM through the production system bus, partitions and SPI SD.
module tb_fdd_sd;
    reg clk48=0,clk=0,reset=1,power_on=1;
    always #10.416667 clk48=!clk48;
    always #20.833333 clk=!clk;
    reg [2:0] mode=6;
    wire cp,cn,cpu_stall;
    wire [2:0] active;
    wire [15:0] ca,cd,cr,a,wd,rd;
    wire [1:0] cl,lanes;
    wire cc,cs,cw,cack,request,write,ready,sys_write,sys_read;
    reg [7:0] sys_in=8'o370;
    bk_cpu_clock clocks(clk48,power_on,mode,!request,cp,cn,active,cpu_stall);
    bk_cpu cpu(cp,cn,reset,1'b0,1'b0,16'o60,16'o177777,{8'o1,sys_in},,,
               ca,cd,cl,cc,cs,cw,cr,cack,sys_write,sys_read,);
    bk_wb_cdc bridge(cp,clk,reset,cc,cs,cw,ca,cd,cl,cack,cr,,request,write,a,wd,lanes,ready,rd,
                     16'b0,);
    reg [7:0] memory[0:262143];
    reg a16m=0,mem_ready=0;
    reg [15:0] mem_read=0;
    wire mem_request,mem_write,locked,storage_idle,sd_cs,sd_sck,sd_mosi,sd_miso;
    wire [19:0] mem_address;
    wire [15:0] mem_data;
    wire [1:0] mem_lanes;
    integer reads=0,b,ignored,cycles=0;
    bk_system_bus bus(
        .clk(clk),.power_on(power_on),.reset(reset),.peripheral_reset(1'b0),.quiesce(1'b0),
        .storage_idle(storage_idle),.cpu_stall(cpu_stall),
        .request(request),.write(write),.address(a),.write_data(wd),.lanes(lanes),.ready(ready),.read_data(rd),
        .mem_request(mem_request),.mem_write(mem_write),.mem_address(mem_address),
        .mem_data(mem_data),.mem_lanes(mem_lanes),.mem_ready(mem_ready),.mem_read(mem_read),
        .boot_data(16'b0),.key_valid(1'b0),.key_code(8'b0),.irq_ack(1'b0),.locked(locked),
        .sd_cs(sd_cs),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),.map_register(16'b0));
    sd_spi_model card(sd_cs,sd_sck,sd_mosi,sd_miso,reads);
    always @(posedge cp)begin
        cycles<=cycles+1;
        if(sys_write)sys_in[2]<=1;else if(sys_read)sys_in[2]<=0;
    end
    reg [15:0] previous_stage=0;
    always @(posedge clk)begin
        if(mem_request&&!mem_ready)begin
            mem_ready<=1;
            mem_read<={memory[mem_address*2+1],memory[mem_address*2]};
            if(mem_write)begin
                if(mem_lanes[0])memory[mem_address*2]<=mem_data[7:0];
                if(mem_lanes[1])memory[mem_address*2+1]<=mem_data[15:8];
                if(mem_address*2==16'o3000)begin
                    $display("SPI FDD stage=%06o CPU mode=%0d cycles=%0d writes=%0d",mem_data,active,cycles,card.writes);$fflush();
                    if(mem_data==16'hffff)$fatal(1,"Native SPI disk ROM failed error=%03o pc=%06o",memory[16'o52],ca);
                    if(mem_data==16'o1111)begin
                        for(b=0;b<512;b=b+1)
                            if(memory[16'o4000+b]!==((b*13)&255))$fatal(1,"Native SPI read byte %0d",b);
                    end
                    previous_stage<=mem_data;
                end
            end
        end else if(!mem_request)mem_ready<=0;
        if(previous_stage==16'o4444&&storage_idle)begin
            if(bus.fdd_error||card.writes<20)$fatal(1,"Native SPI writes/format were not drained");
            $display("PASS native %s through system bus/raw partitions/SPI %s: mode=%0d reads=%0d writes=%0d",
                a16m?"A16M":"DISK 327",card.high_capacity?"SDHC":"SDSC",active,reads,card.writes);
            $finish;
        end
    end
    // Preload the same descriptor table that the resident BIOS seals. The
    // cold-boot suite separately exercises its native firmware and ROM CRC.
    task config_word(input [1:0] address,input [15:0] value);
        begin
            @(negedge clk);
            force bus.disk_config_address=address;
            force bus.disk_config_data=value;
            force bus.disk_config_write=1;
            @(negedge clk);force bus.disk_config_write=0;
            repeat(2)@(negedge clk);
        end
    endtask
    initial begin
        ignored=$value$plusargs("mode=%d",mode);
        a16m=$test$plusargs("a16m");
        for(b=0;b<262144;b=b+1)memory[b]=0;
        $readmemh("build/fdd-program.hex",memory,16'o400,16'o400+147);
        $readmemh(a16m?"build/diska16m.hex":"build/disk327.hex",memory,
                     a16m?20'h28000:20'h2a000,a16m?20'h28fff:20'h2afff);
        #200;power_on=0;
        repeat(40)@(negedge clk);
        config_word(1,0);
        config_word(2,34816);config_word(2,0);config_word(2,1600);config_word(2,0);
        config_word(2,36864);config_word(2,0);config_word(2,1600);config_word(2,0);
        repeat(8)config_word(2,0);
        config_word(2,2048);config_word(2,0);config_word(2,32768);config_word(2,0);
        config_word(2,38912);config_word(2,0);
        config_word(0,card.high_capacity?16'h8201:16'h8200);
        wait(bus.disk_configured);
        release bus.disk_config_write;release bus.disk_config_address;release bus.disk_config_data;
        @(negedge clk);bus.model=a16m?0:3;bus.locked=1;bus.boot_stage=8'hff;
        card.idle=0;
        #200;reset=0;
    end
    initial begin #4000000000;$fatal(1,"Native SPI FDD timeout stage=%06o pc=%06o",previous_stage,ca);end
endmodule
