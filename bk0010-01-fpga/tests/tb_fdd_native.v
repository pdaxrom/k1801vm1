`timescale 1ns/1ps
// Native DISK 327/A16M procedures execute on cpu11. A sector-only SD backend model
// separates controller/mechanical timing from the SPI host's own tests.
module tb_fdd_native;
    reg clk48=0,clk=0,reset=1,power_on=1;
    always #10.416667 clk48=!clk48;
    always #20.833333 clk=!clk;
    reg [2:0] mode=1;
    wire cp,cn;
    wire [2:0] active;
    bk_cpu_clock clocks(clk48,power_on,mode,!request,cp,cn,active,
                        request&&fdd_select&&!fdd_ready);
    wire [15:0] ca,cd,cr,a,wd,rd;
    wire [1:0] cl,lanes;
    wire cc,cs,cw,cack,request,write,ready,sys_write,sys_read;
    reg [7:0] sys_in=8'o370;
    bk_cpu cpu(cp,cn,reset,1'b0,1'b0,16'o60,16'o177777,{8'o1,sys_in},,,
               ca,cd,cl,cc,cs,cw,cr,cack,sys_write,sys_read,);
    bk_wb_cdc bridge(cp,clk,reset,cc,cs,cw,ca,cd,cl,cack,cr,,request,write,a,wd,lanes,ready,rd,
                     16'b0,);
    reg [7:0] memory[0:262143],disk[0:1638399],buffer[0:511];
    reg a16m=0;
    reg [6:0] a16_mode=7'o160;
    reg [1:0] a16_phase=0;
    wire mapped;
    wire [19:0] mapped_address;
    bk_memory_map mapper(3'b0,16'b0,a16_mode,a,write,mapped,mapped_address);
    wire [19:0] memory_address=a16m?mapped_address:{4'b0,a};
    wire memory_ready=a16m?mapped:(a<16'o177600&&(!write||a<16'o100000));
    wire fdd_select=(a&16'o177774)==16'o177130;
    wire fdd_ready,fdd_idle,fdd_error;
    wire [15:0] fdd_data;
    wire sector_request,sector_write;
    wire [1:0] sector_drive;
    wire [10:0] sector_offset;
    wire [8:0] ba;
    wire bw;
    wire [7:0] bd;
    reg [7:0] br=0;
    reg sector_ready=1,sector_done=0;
    reg [3:0] sector_delay=0;
    integer reads=0,writes=0,b,s,d,ignored,cycles=0;
    reg handled=0;
    bk_fdd controller(clk,power_on,1'b0,1'b0,request&&fdd_select,write,a[1],wd,
        fdd_ready,fdd_data,fdd_idle,fdd_error,4'b0011,4'b0010,
        sector_request,sector_write,sector_drive,sector_offset,sector_ready,sector_done,1'b0,ba,bw,bd,br);
    assign ready=fdd_select?fdd_ready:(request&&memory_ready);
    assign rd=fdd_select?fdd_data:{memory[{memory_address[17:1],1'b0}+18'd1],memory[{memory_address[17:1],1'b0}]};
    always @(posedge cp)begin
        cycles<=cycles+1;
        if(sys_write)sys_in[2]<=1;else if(sys_read)sys_in[2]<=0;
    end
    reg [15:0] previous_stage=0;
    always @(posedge clk)begin
        if(!request)handled<=0;
        else if(ready&&!handled)begin
            handled<=1;
            if(a16m&&write&&fdd_select&&!a[1])begin
                a16_mode[3:2]<=wd[3:2];
                if(a16_phase!=0)begin
                    a16_mode[6:4]<=wd[6:4];
                    a16_phase<=(wd[2:1]==3)?1:0;
                end else if(wd[2:1]==3)a16_phase<=1;
            end
            if(write&&!fdd_select&&memory_ready)begin
                if(lanes[0])memory[{memory_address[17:1],1'b0}]<=wd[7:0];
                if(lanes[1])memory[{memory_address[17:1],1'b0}+18'd1]<=wd[15:8];
                if(a==16'o3000)begin
                    $display("FDD stage=%06o CPU mode=%0d cycles=%0d writes=%0d",wd,active,cycles,writes);$fflush();
                    if(wd==16'hffff)$fatal(1,"Native disk ROM failed error=%03o pc=%06o ir=%06o status=%06o",memory[16'o52],ca,cpu.core.ir,controller.status);
                    if(wd==16'o1111)begin
                        for(b=0;b<512;b=b+1)
                            if(memory[16'o4000+b]!==disk[b])$fatal(1,"Native read byte %0d",b);
                    end
                    previous_stage<=wd;
                end
            end
        end
        br<=buffer[ba];if(bw&&sector_ready)buffer[ba]<=bd;
        sector_done<=0;
        if(sector_request&&sector_ready)begin sector_ready<=0;sector_delay<=8;end
        else if(sector_delay!=0)begin
            sector_delay<=sector_delay-1'b1;
            if(sector_delay==1)begin
                if(sector_drive>1||sector_offset>=1600)$fatal(1,"Native controller escaped raw drive geometry");
                if(sector_write)begin
                    if(sector_drive!=0)$fatal(1,"Native ROM wrote protected drive B");
                    for(b=0;b<512;b=b+1)disk[(sector_drive*1600+sector_offset)*512+b]<=buffer[b];
                    writes<=writes+1;
                end else begin
                    for(b=0;b<512;b=b+1)buffer[b]<=disk[(sector_drive*1600+sector_offset)*512+b];
                    reads<=reads+1;
                end
                sector_done<=1;
            end
        end else if(!sector_request)sector_ready<=1;
        if(previous_stage==16'o4444&&fdd_idle)begin
            if(fdd_error||writes<20)$fatal(1,"Native write/format was not drained");
            for(b=0;b<5120;b=b+1)if(disk[b]!==8'h42)$fatal(1,"Native format byte %0d got=%02h",b,disk[b]);
            $display("PASS cpu11 native %s read/write/RO/format: CPU mode=%0d reads=%0d writes=%0d",
                     a16m?"A16M":"DISK 327",active,reads,writes);
            $finish;
        end
    end
    initial begin
        ignored=$value$plusargs("mode=%d",mode);
        a16m=$test$plusargs("a16m");
        for(b=0;b<262144;b=b+1)memory[b]=0;
        for(d=0;d<2;d=d+1)for(s=0;s<1600;s=s+1)for(b=0;b<512;b=b+1)
            disk[(d*1600+s)*512+b]=d+s*31+b*13;
        $readmemh("build/fdd-program.hex",memory,16'o400,16'o400+147);
        if(a16m)$readmemh("build/diska16m.hex",memory,20'h28000,20'h28fff);
        else $readmemh("build/disk327.hex",memory,16'o160000,16'o170000-1);
        #200;power_on=0;#200;reset=0;
    end
    initial begin #3000000000;$fatal(1,"Native FDD timeout stage=%06o pc=%06o ir=%06o status=%06o cache=%0d pos=%0d",previous_stage,ca,cpu.core.ir,controller.status,controller.cache.state,controller.position);end
endmodule
