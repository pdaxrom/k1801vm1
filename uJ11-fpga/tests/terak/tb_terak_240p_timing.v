`timescale 1ns/1ps
module tb_terak_240p_timing;
    reg clk=0,reset=1;
    always #7.8125 clk=~clk;
    wire [11:0] x;
    wire [10:0] half_line;
    wire [9:0] line_number;
    wire line_start,frame_start,field_start,field,sync,active_line,active,prefetch;
    wire [7:0] row,next_row;
    uj11_terak_240p_timing timing(.*);
    integer cycle=0,half_index=0,half_origin=0,half_end=2035,frame_index=0;
    integer pulse_clocks=0,rows=0,prefetches=0,broad_pulses=0,equal_pulses=0,line_pulses=0;
    integer h,length,expected_x;
    reg is_broad,is_equal,is_active,is_prefetch;
    always @(posedge clk)begin
        if(reset)begin
            cycle=0;half_index=0;half_origin=0;half_end=2035;frame_index=0;
            pulse_clocks=0;rows=0;prefetches=0;broad_pulses=0;equal_pulses=0;line_pulses=0;
        end else begin
            if(cycle==half_end)begin
                h=half_index%524;
                length=half_end-half_origin;
                is_broad=h<6;
                is_equal=(h>=6 && h<12) || h>=518;
                if(pulse_clocks!=(is_broad ? length-301 : is_equal ? 147 : h%2==0 ? 301 : 0))
                    $fatal(1,"sync width half=%0d length=%0d width=%0d",h,length,pulse_clocks);
                if(is_broad)broad_pulses++;
                else if(is_equal)equal_pulses++;
                else if(h%2==0)line_pulses++;
                if(h==523)begin
                    if(broad_pulses!=6 || equal_pulses!=12 || line_pulses!=253)
                        $fatal(1,"262-line sync train %0d/%0d/%0d",broad_pulses,equal_pulses,line_pulses);
                    broad_pulses=0;equal_pulses=0;line_pulses=0;
                end
                half_origin=half_end;half_index++;
                // Independent rational clock count, including the accumulator
                // remainder carried across complete 262-line frames.
                half_end=(64'(half_index+1)*800000)/393;
                pulse_clocks=0;
            end
            h=half_index%524;
            expected_x=cycle-(64'(half_index/2)*1600000)/393;
            if(half_line!==h || x!==expected_x || line_number!==h/2 || line_start!==(expected_x==0))
                $fatal(1,"position cycle=%0d half=%0d/%0d x=%0d/%0d",cycle,half_line,h,x,expected_x);
            if(field!==0 || frame_start!==((h==0) && cycle==half_origin) || field_start!==frame_start)
                $fatal(1,"progressive frame boundary");
            if(frame_start)begin
                if(x!==0 || cycle!=(64'(frame_index)*3200000)/3)
                    $fatal(1,"60 Hz frame drift or changing horizontal phase at %0d",cycle);
                if(frame_index && (rows!=240 || prefetches!=240))
                    $fatal(1,"frame rows %0d/%0d",rows,prefetches);
                rows=0;prefetches=0;frame_index++;
                if(frame_index==7)begin
                    $display("PASS 262 complete lines, exact 60 Hz over 6 frames, constant scan phase, sync widths and 240 rows/frame");
                    $finish;
                end
            end
            is_active=h/2>=18 && h/2<258;
            is_prefetch=h/2>=17 && h/2<257;
            if(active_line!==is_active || prefetch!==is_prefetch ||
                active!==(is_active && x>=768 && x<3968))$fatal(1,"active/prefetch window");
            if(line_start && active_line)begin
                if(row!==rows)$fatal(1,"row sequence %0d/%0d",row,rows);
                rows++;
            end
            if(line_start && prefetch)begin
                if(next_row!==prefetches)$fatal(1,"prefetch sequence %0d/%0d",next_row,prefetches);
                prefetches++;
            end
            if(sync)pulse_clocks++;
            cycle++;
        end
    end
    initial begin
        repeat(4)@(negedge clk);reset=0;
        repeat(3000)@(negedge clk);reset=1;
        repeat(4)@(negedge clk);reset=0;
    end
    initial begin #110000000;$fatal(1,"timing timeout");end
endmodule
