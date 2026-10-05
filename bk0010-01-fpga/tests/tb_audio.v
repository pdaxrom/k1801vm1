`timescale 1ns/1ps
module tb_audio;
    reg clk48=0,reset=1,port_write=0,beeper=0,stall=0;
    always #10.416667 clk48=!clk48;
    reg [1:0] lanes=0;
    reg [15:0] data=0;
    reg [2:0] mode=0;
    wire cpu_clk,cpu_n,audio;
    wire [2:0] active;
    bk_cpu_clock clocks(clk48,reset,mode,1'b1,cpu_clk,cpu_n,active,stall);
    bk_audio dut(cpu_clk,clk48,reset,port_write,lanes,data,beeper,audio);
    integer accepted=0,submitted=0,cycle=0,j,edges,before_ticks,ticks=0,ones;
    reg old_tone;
    always @(posedge clk48) if(!dut.audio_reset) begin
        cycle=cycle+1;
        if(dut.tick) ticks=ticks+1;
        if(dut.command_valid) accepted=accepted+1;
    end
    task command(input [1:0] select,input [15:0] value,input integer width);
        begin
            @(negedge cpu_clk); lanes=select; data=value; port_write=1;
            repeat(width) @(negedge cpu_clk);
            port_write=0; submitted=submitted+1;
            @(negedge cpu_clk);
        end
    endtask
    task reg_write(input [3:0] index,input [7:0] value);
        begin
            command(3,~{12'b0,index},1);
            command(1,{8'b0,~value},1);
        end
    endtask
    task drain;
        begin
            repeat(16) @(negedge clk48);
            if(accepted!=submitted) $fatal(1,"AY FIFO lost/duplicated commands %0d/%0d",accepted,submitted);
        end
    endtask
    initial begin
        #200; reset=0; repeat(16) @(negedge clk48);
        if(audio || dut.ay_pcm) $fatal(1,"Audio reset not silent");
        for(j=0;j<7;j=j+1) begin
            mode=j; wait(active==mode);
            // Word selects, byte writes; long strobes must remain single writes.
            command(3,16'hfff8,3); command(1,16'hffc0,3); // all tone/noise disabled
            reg_write(8,15); reg_write(9,3); reg_write(10,0); drain();
            if(dut.psg.registers[7]!=63 || dut.ay_pcm!=130) $fatal(1,"AY word/byte decode mode %0d R7=%0d PCM=%0d R8=%0d R9=%0d",j,dut.psg.registers[7],dut.ay_pcm,dut.psg.registers[8],dut.psg.registers[9]);
            command(3,16'hfff9,1); command(2,16'hf100,1); drain(); // upper byte -> R6=14
            if(dut.psg.registers[6]!=14) $fatal(1,"AY upper byte decode");
            reg_write(1,8'hff); reg_write(6,8'hff); reg_write(8,8'hff); drain();
            if(dut.psg.registers[1]!=15 || dut.psg.registers[6]!=31 || dut.psg.registers[8]!=31)
                $fatal(1,"AY unused bits were not masked");
            reg_write(0,100); reg_write(1,0); reg_write(8,15); reg_write(9,0); reg_write(7,62); drain();
            before_ticks=ticks; edges=0; old_tone=dut.psg.tone[0];
            repeat(48000) begin
                @(negedge clk48);
                if(old_tone!=dut.psg.tone[0]) begin edges=edges+1; old_tone=dut.psg.tone[0]; end
            end
            if(ticks-before_ticks<221 || ticks-before_ticks>222 || edges<2 || edges>3)
                $fatal(1,"AY pitch changed with CPU mode %0d: ticks=%0d edges=%0d",j,ticks-before_ticks,edges);
        end
        $display("PASS AY CPU protocol/FIFO and fixed pitch at all seven CPU frequencies");
        stall=1; repeat(12) @(negedge clk48);
        before_ticks=ticks; repeat(48000) @(negedge clk48);
        if(ticks-before_ticks<221 || ticks-before_ticks>222) $fatal(1,"AY stopped with CPU disk wait");
        stall=0;
        reg_write(7,63); reg_write(8,15); reg_write(9,15); reg_write(10,15); beeper=1; drain();
        if(dut.mixed!=509) $fatal(1,"AY/beeper mixer clips");
        ones=0;
        repeat(512) begin @(negedge clk48); if(audio) ones=ones+1; end
        if(ones!=509) $fatal(1,"Sigma delta density %0d expected 509",ones);
        reg_write(8,0); reg_write(9,0); reg_write(10,0); drain();
        ones=0; repeat(512) begin @(negedge clk48); if(audio) ones=ones+1; end
        if(ones!=128) $fatal(1,"Beeper mixing density %0d",ones);
        // Reset while queued commands are still crossing cannot replay them.
        @(negedge cpu_clk); port_write=1; lanes=3; data=16'hfff8;
        @(negedge cpu_clk); reset=1; beeper=0; port_write=0;
        #100; if(audio) $fatal(1,"Audio not muted during reset");
        reset=0; repeat(32) @(negedge clk48);
        if(dut.ay_pcm || audio || dut.selected) $fatal(1,"AY reset left stale commands");
        $display("PASS AY runs through disk wait, bounded mixing, sigma delta and reset");
        $finish;
    end
    initial begin #100000000; $fatal(1,"AY audio test timeout"); end
endmodule
