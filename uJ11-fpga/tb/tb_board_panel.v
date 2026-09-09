`timescale 1ns/1ps
// Exercise the actual physical top, shared serial pins and bidirectional TDO.
// CPU bus signals are driven here only to isolate the peripheral contract.
module tb_board_panel;
    reg res=0;
    wire clk=dut.clk;
    wire din,ce,sck,rs,blank,latch;
    tri [3:0] rows;
    reg host_mode=0;reg [2:0] host_inputs=0;
    reg [3:0] keyboard_rows=0;
    assign rows[2:0]=host_mode ? host_inputs : keyboard_rows[2:0];
    assign rows[3]=host_mode ? 1'bz : keyboard_rows[3];
    reg req=0,wr=0;reg [1:0] lanes=3;
    reg [15:0] address=16'o166000,data=0;
    reg [7:0] shadow=8'h12,shifted=0,outputs=0;
    reg [639:0] display_bits=0,expected_display=0;
    integer display_count=0,expected_count=0,frames=0,edges=0,checks=0,i,j,k;
    reg [15:0] answer;
    reg [7:0] value;
    uj11_hc1200_microcomp dut(.res(res),.rx(1'b1),.tx(),
        .sd_cs_n(),.sd_sck(),.sd_mosi(),.sd_miso(1'b1),
        .gpio_mosi(),.gpio_miso(1'b1),.gpio_msck(),.gpio_mcs(),
        .gpio_din(din),.gpio_ce(ce),.gpio_clk(sck),.gpio_rs(rs),
        .gpio_blank(blank),.gpio_reg_latch(latch),.gpio_key_row(rows));
    always @(posedge sck)begin
        shifted={shifted[6:0],din};edges=edges+1;
        if(!ce)begin display_bits={display_bits[638:0],din};display_count=display_count+1;end
    end
    always @(posedge latch)outputs=shifted;
    always @(negedge ce)begin display_count=0;display_bits=0;end
    always @(posedge ce)if(res && display_count!=0)frames=frames+1;
    task beat(input bit writing,input [1:0] mask,input [15:0] a,d);
        begin
            @(negedge clk);req=1;wr=writing;lanes=mask;address=a;data=d;
            @(posedge clk);#1;
            if(dut.system.raw_ack!==1)$fatal(1,"panel did not ACK");
            answer=dut.system.lane_rdata;checks=checks+1;
            @(negedge clk);req=0;repeat(2)@(negedge clk);
        end
    endtask
    task put(input [7:0] p);
        begin shadow=p;beat(1,2,16'o166001,{p,8'b0});end
    endtask
    task shift_byte(input [7:0] b);
        integer bit_no;
        begin
            for(bit_no=7;bit_no>=0;bit_no=bit_no-1)begin
                put((shadow&8'hfe)|{7'b0,b[bit_no]});
                put(shadow|8'h04);put(shadow&8'hfb);
            end
        end
    endtask
    task shift_output(input [7:0] b);
        begin
            put((shadow|8'h02)&8'hdf);shift_byte(b);
            put(shadow|8'h20);put(shadow&8'hdf);
            if(outputs!==b)$fatal(1,"external shift-register order");
        end
    endtask
    initial begin
        force dut.system.bus_request=req;
        force dut.system.writing=wr;
        force dut.system.address=address;
        force dut.system.lanes=lanes;
        force dut.system.lane_data=data;
        repeat(8)@(negedge clk);res=1;repeat(6)@(negedge clk);
        if({dut.host_miso_oe,dut.host_miso,latch,blank,rs,sck,ce,din}!==8'h12)
            $fatal(1,"panel reset pins");
        // A pin transition cannot bypass either synchronizer stage.
        keyboard_rows=4'ha;#1;
        if(dut.system.bus.panel_rdata[7:4]!==0)$fatal(1,"asynchronous row bypass");
        @(posedge clk);#1;
        if(dut.system.bus.panel_rdata[7:4]!==0)$fatal(1,"row bypassed second stage");
        @(posedge clk);#1;
        if(dut.system.bus.panel_rdata[7:4]!==4'ha)$fatal(1,"row synchronizer latency");
        @(negedge clk);keyboard_rows=0;repeat(3)@(negedge clk);
        // Low byte is read-only; all high-byte output bits read back.
        beat(1,1,16'o166000,16'h00ff);
        beat(0,3,16'o166000,0);if(answer!==16'h1200)$fatal(1,"read-only row lane");
        // The original PNLDRV command stream: eight 0x4c bytes, MSB first.
        put(8'h1a);put(shadow&8'hfd);expected_display=0;
        for(i=0;i<8;i=i+1)begin shift_byte(8'h4c);expected_display={expected_display[631:0],8'h4c};end
        put(shadow|8'h02);
        if(display_count!=64 || display_bits!==expected_display || rs!==1)$fatal(1,"HCMS command frame");
        // A complete 16-character dot frame (80 bytes), with independent blanking.
        put(shadow&8'hf5);expected_display=0;
        for(i=0;i<80;i=i+1)begin
            value=8'((i*37)^165);shift_byte(value);expected_display={expected_display[631:0],value};
        end
        put(shadow|8'h02);put(shadow&8'hef);
        if(display_count!=640 || display_bits!==expected_display || blank!==0 || rs!==0)
            $fatal(1,"HCMS 80-byte frame/blank");
        for(i=0;i<8;i=i+1)begin
            shift_output(8'hf8|8'(7^i));
            if(outputs[2:0]!==3'(7^i) || frames!=2)$fatal(1,"RGB or shared display select");
        end
        // Twenty matrix positions: column bits 3..7, row bits 0..3.
        for(i=0;i<5;i=i+1)for(j=0;j<4;j=j+1)begin
            shift_output((8'h08<<i)|8'h07);
            keyboard_rows=4'(1<<j);repeat(3)@(negedge clk);
            beat(0,3,16'o166000,0);
            if(answer[7:4]!==keyboard_rows || outputs[7:3]!==5'(1<<i))$fatal(1,"matrix position");
        end
        keyboard_rows=0;
        // HG releases columns before TDO ownership; TMS/TCK/TDI stay inputs.
        shift_output(8'hff);host_mode=1;put((shadow&8'h3f)|8'hc0);
        if(rows[3]!==1 || outputs!==8'hff)$fatal(1,"HG request/TDO ownership");
        for(k=0;k<16;k=k+1)begin
            host_inputs=3'(k);repeat(3)@(negedge clk);
            beat(0,3,16'o166000,0);
            if(answer[6:4]!==host_inputs)$fatal(1,"HG input mapping");
            put((shadow&8'hbf)|{1'b0,k[0],6'b0});
            if(rows[3]!==k[0])$fatal(1,"HG TDO bit");
        end
        put(shadow&8'h3f);if(rows[3]!==1'bz)$fatal(1,"HG TDO release");
        // Reset must also release the host output, even while it owns TDO.
        put(shadow|8'hc0);res=0;repeat(4)@(negedge clk);
        if(rows[3]!==1'bz || blank!==1 || ce!==1)$fatal(1,"reset did not release TDO");
        $display("PASS panel physical top: %0d bus beats; HCMS command/640 dots, 8 RGB colors, 20 keys, HG inputs/TDO/Z/reset",checks);
        $finish;
    end
    initial begin #2000000;$fatal(1,"panel watchdog");end
endmodule

// Simulation-only OSCH source. Production instantiates the Lattice primitive.
module OSCH #(parameter NOM_FREQ="29.56")(input STDBY,output reg OSC=0);
    always #5 if(!STDBY)OSC=~OSC;
endmodule
