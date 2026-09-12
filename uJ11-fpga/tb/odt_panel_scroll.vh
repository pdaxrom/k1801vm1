// Included by the electrical panel bench. Only guest input memory is seeded;
// the real monitor computes RESULT, SCROLL, SCREEN and every display pin.
reg [7:0] font[0:319];
reg [15:0] scroll_regs[0:8],scroll_meta,scroll_memory[0:28671];
integer user_accesses=0,scroll_accesses=0,scroll_windows=0;

always @(posedge clk)if(!reset && ack && !bank && !io)
    user_accesses<=user_accesses+1;

function [7:0] upper_byte(input integer a);
    reg [15:0] word_value;
    begin
        word_value=upper(a);
        return a[0] ? word_value[15:8] : word_value[7:0];
    end
endfunction

task panel_check(input bit ok,input string why);
    begin if(!ok)$fatal(1,"%s",why);checks=checks+1;end
endtask

task scroll_snapshot;
    begin
        for(integer i=0;i<9;i=i+1)scroll_regs[i]=upper(O_REGS+2*i);
        scroll_meta=upper(O_META);scroll_accesses=user_accesses;
        for(integer i=0;i<28672;i=i+1)scroll_memory[i]=memory[i];
    end
endtask

task scroll_unchanged;
    begin
        for(integer i=0;i<9;i=i+1)eq(upper(O_REGS+2*i),scroll_regs[i],"scroll preserves saved R0-R7/PSW");
        eq(upper(O_META),scroll_meta,"scroll preserves stop metadata");
        panel_check(user_accesses==scroll_accesses,"scroll must not fetch/read/write USER memory");
        for(integer i=0;i<28672;i=i+1)
            if(memory[i]!==scroll_memory[i])$fatal(1,"scroll modified USER memory %o",i*2);
        panel_check(1,"USER RAM snapshot unchanged");
    end
endtask

task scroll_window(input string text,input integer offset);
    string window_text;
    reg [639:0] golden;
    integer i,j,k,c;
    begin
        eq(upper(O_SCROLL),16'(offset),"scroll offset");
        window_text="";golden=0;
        for(i=0;i<16;i=i+1)begin
            c=i+offset<text.len() ? int'(text[i+offset]) : 32;
            window_text={window_text,8'(c)};
            eq({8'b0,upper_byte(O_SCREEN+i)},16'(c),"HDSP character window");
        end
        // Chain transmits the right module first, five font bytes per cell.
        for(i=0;i<16;i=i+1)begin
            k=(i+8)%16;c=int'(window_text[k]);
            for(j=0;j<5;j=j+1)golden={golden[631:0],font[(c-32)*5+j]};
        end
        panel_check(last_display===golden,"HDSP 640-bit frame matches window and module order");
        for(i=0;i<text.len();i=i+1)
            if(upper_byte(O_RESULT+i)!==text[i])$fatal(1,"scroll modified result byte %0d",i);
        eq({8'b0,upper_byte(O_RESULT+text.len())},0,"result terminator");
        scroll_windows=scroll_windows+1;
    end
endtask

task scroll_key(input integer key,input string text,input integer offset);
    begin
        segment="";keypress(key);
        panel_check(segment=="","panel scrolling must not emit UART or execute another command");
        scroll_window(text,offset);scroll_unchanged();
    end
endtask

task scroll_uart(input string action,input string text,input integer offset);
    begin
        command(action);
        panel_check(segment=={action,8'd13,8'd10,"ODT> "},"UART scroll emits only echo and prompt");
        scroll_window(text,offset);scroll_unchanged();
    end
endtask

task scroll_regression;
    string line_text;
    integer i,limit,held_frames;
    begin
        line_text="143042: 004567 177324  JSR R5,142372";
        limit=line_text.len()-16;
        command("D 143042");contains(line_text);scroll_window(line_text,0);
        scroll_snapshot();
        scroll_key(17,line_text,0); // PREV at the left edge
        for(i=1;i<=limit;i=i+1)scroll_key(18,line_text,i);
        repeat(2)scroll_key(18,line_text,limit); // right edge clamps
        for(i=limit-1;i>=0;i=i-1)scroll_key(17,line_text,i);
        scroll_key(17,line_text,0);
        for(i=1;i<=limit;i=i+1)scroll_uart(">",line_text,i);
        scroll_uart(">",line_text,limit);
        for(i=limit-1;i>=0;i=i-1)scroll_uart("<",line_text,i);
        scroll_uart("<",line_text,0);

        // Held NEXT is one press: this firmware intentionally has no repeat.
        segment="";@(negedge clk);keycode=18;
        while(upper(O_KLAST)!=18)@(negedge clk);repeat(500000)@(negedge clk);
        scroll_window(line_text,1);held_frames=display_frames;
        repeat(1000000)@(negedge clk);
        scroll_window(line_text,1);
        panel_check(display_frames==held_frames,"held NEXT must not redraw/repeat");
        panel_check(segment=="","held NEXT must not print UART");
        keycode=-1;while(upper(O_KLAST)!=65535)@(negedge clk);repeat(10000)@(negedge clk);
        scroll_unchanged();

        // K must change dispatch, not the electrical matrix or guest context.
        command("K 23 22 21 20");contains("FUNCTION KEYS SET");
        command("D 143042");scroll_window(line_text,0);scroll_snapshot();
        scroll_key(17,line_text,1);scroll_key(18,line_text,0);
        command("K 20 21 22 23");contains("FUNCTION KEYS SET");

        // ENTER advances to the instruction after its extension word.
        command("D 143042");scroll_snapshot();scroll_key(18,line_text,1);
        segment="";keypress(19);contains("[PANEL] D 143046");
        contains("143046: 000240  NOP");scroll_window("143046: 000240  NOP",0);
        command("R 0");contains("R0=052525");scroll_window("R0=052525",0);
        scroll_snapshot();scroll_uart(">","R0=052525",0);scroll_uart("<","R0=052525",0);
        $display("PASS CP64 scroll regression: %0d windows; photo line, panel/UART edges, held key, remap, ENTER, padding, context",scroll_windows);
    end
endtask
