// CP77 adds one menu entry, with physical PREV/NEXT/ENTER and UART mirroring.
segment="";keypress(19);contains("[PANEL] F 000000");contains("AC0=040200 000001 000002 000003");
eq(upper(O_VIEW),4,"FP register view");eq(upper(O_FSEL),0,"FP menu starts AC0");
frames_before=display_frames;repeat(500000)@(negedge clk);
panel_check(frames_before==display_frames,"FP page waits for a key");
segment="";keypress(18);contains("AC1=");eq(upper(O_FSEL),1,"FP NEXT");
segment="";keypress(19);contains("AC2=");eq(upper(O_FSEL),2,"FP ENTER advances, never edits");
eq(upper(O_PANEDI),0,"no FP numeric editor");
for(integer i=3;i<9;i++)begin
    segment="";keypress(18);eq(upper(O_FSEL),16'(i),"FP NEXT one row");
    if(i<6)contains($sformatf("AC%0d=",i));
    else if(i==6)contains("FPS=000300 MODE=D,L");
    else if(i==7)contains("FEC=");else contains("FEA=");
end
keypress(18);eq(upper(O_FSEL),0,"FP NEXT wrap");
keypress(17);eq(upper(O_FSEL),8,"FP PREV wrap");
frames_before=display_frames;command("F");
for(integer i=0;i<6;i++)contains($sformatf("AC%0d=",i));
contains("FPS=");contains("FEC=");contains("FEA=");
panel_check(display_frames==frames_before+1,"UART F renders one panel row");
scroll_window("AC0=040200 000001 000002 000003",0);
command("A 1");photo_line="AC0=040200 000001 000002 000003";
for(integer offset=1;offset<=(photo_line.len()-16);offset++)begin
    frames_before=display_frames;while(display_frames==frames_before)@(negedge clk);
    @(negedge clk);scroll_window("AC0=040200 000001 000002 000003",offset);
end
for(integer offset=(photo_line.len()-17);offset>=0;offset--)begin
    frames_before=display_frames;while(display_frames==frames_before)@(negedge clk);
    @(negedge clk);scroll_window("AC0=040200 000001 000002 000003",offset);
end
command("A 0");keypress(10);eq(upper(O_VIEW),0,"A still selects CPU registers");
$display("PASS CP77: %0d checks %0d windows %0d clocks",checks,scroll_windows,cycles);$finish;
