`timescale 1ns/1ps
module tb_terak_video_probe #(parameter PAL50=0,FASTER=0,MEASURE=0,PROGRESSIVE=0);
    reg clk=0,reset=1;
    always #7.8125 clk=~clk;
    wire [5:0] dac;
    localparam X_FIRST=FASTER ? 695 : PAL50 ? 685 : 768;
    localparam PICTURE_CLOCKS=FASTER ? 3180 : 3200;
    localparam ROWS=MEASURE ? 247 : 240, FIRST_LABEL=MEASURE ? 12 : 0;
    uj11_terak_video_probe #(.PAL50(PAL50),.FASTER(FASTER),.MEASURE(MEASURE),.PROGRESSIVE(PROGRESSIVE)) probe(.*);
    integer fields=0,lines=0,pixels=0,output_file;
    integer sample_x;
    integer warm=0;
    integer cycles=0;
    integer next_pixel=0;
    reg [2:0] sync_history=0,active_history=0;
    string raster;
    always @(posedge clk)begin
        if(reset)begin warm=0;cycles=0;sync_history<=0;active_history<=0;end
        else begin
            warm++;
            sync_history<={sync_history[1:0],probe.sync};
            active_history<={active_history[1:0],probe.active};
        end
    end
    always @(posedge clk)if(!reset)begin
        if(probe.field_start)begin
            if(cycles!=(PAL50 ? fields*1280000 : fields*3200000/3))
                $fatal(1,"field frequency/phase mode=%0d fields=%0d cycles=%0d",PAL50,fields,cycles);
            fields++;
            if(fields==3)begin
                if(lines!=ROWS*2 || pixels!=640*ROWS*2)$fatal(1,"incomplete diagnostic raster %0d/%0d",lines,pixels);
                $fclose(output_file);
                $display("PASS PAL50=%0d FASTER=%0d MEASURE=%0d PROGRESSIVE=%0d: refresh frequency, DAC sync/blanking, two %0d-row scans, 640 pixels/row, borders and row labels",PAL50,FASTER,MEASURE,PROGRESSIVE,ROWS);
                $finish;
            end
        end
        cycles++;
    end
    always @(negedge clk)if(!reset)begin
        if(warm>=3)begin
            if((dac===0)!==sync_history[2])$fatal(1,"DAC sync waveform or phase changed");
            if(!sync_history[2] && !active_history[2] && dac!==15)
                $fatal(1,"DAC blanking waveform or phase changed");
        end
        // Two extra DAC pipeline clocks delay the complete waveform equally.
        sample_x=probe.x-2;
        if(probe.active_line && sample_x==X_FIRST+1)begin
            lines++;
            next_pixel=0;
            if(MEASURE && probe.line_number-(probe.field ? 263 : 0)!=probe.row+12)
                $fatal(1,"physical field-line labels");
            if(probe.digits[11:8]*100+probe.digits[7:4]*10+probe.digits[3:0]!=probe.row+FIRST_LABEL)
                $fatal(1,"row number %h/%0d",probe.digits,probe.row);
            if(probe.label_digits[11:8]*100+probe.label_digits[7:4]*10+probe.label_digits[3:0]!=(probe.row+FIRST_LABEL)/8*8)
                $fatal(1,"ruler label %h/%0d",probe.label_digits,probe.row);
        end
        if(probe.active_line && sample_x>=X_FIRST+1 && sample_x<=X_FIRST+PICTURE_CLOCKS)begin
            if((probe.row==0 || probe.row==ROWS-1 || (MEASURE && (probe.row==6 || probe.row==245)) || sample_x<=X_FIRST+5 ||
                sample_x>=X_FIRST+PICTURE_CLOCKS-4) && dac!==50)
                $fatal(1,"native picture border row=%0d x=%0d DAC=%0d",probe.row,probe.x,dac);
            // Independent closed-form start time: one shortened pulse after
            // columns 15,47,...623, rather than copying the RTL phase counter.
            if(next_pixel<640 && sample_x==X_FIRST+1+5*next_pixel-
                (FASTER ? (next_pixel+16)/32 : 0))begin
                $fwrite(output_file,"%02x\n",dac);pixels++;next_pixel++;
            end
        end
    end
    initial begin
        if(!$value$plusargs("RASTER=%s",raster))raster="probe-raster.hex";
        output_file=$fopen(raster,"w");if(!output_file)$fatal(1,"cannot write raster");
        repeat(4)@(negedge clk);reset=0;
    end
    initial begin #50000000;$fatal(1,"diagnostic timeout");end
endmodule
