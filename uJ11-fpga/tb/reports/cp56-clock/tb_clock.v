`timescale 1ns/1ps
// Board-delay envelope, not measured PCB delays. Sweep both clock corners
// and independent outgoing/incoming delays; make MISO invalid until tCLQV.
module tb_spi_cp56 #(
    parameter real HALF_NS=16.91475, SCK_NS=4.0, DATA_NS=4.0, CS_NS=4.0,
    parameter real RETURN_NS=1.0, VALID_NS=13.0, HIGH_NS=HALF_NS, LOW_NS=HALF_NS
);
    reg clk=0,rst=1,req=0,writing=0,byte_access=0,bank=0,keep_read=1,close_read=0;
    reg [15:0] address=0,data=0;
    wire [15:0] value;
    wire ready,error,busy,cs,sck,mosi,miso;
    wire pin_cs,pin_sck,pin_mosi,ideal_so;
    reg fram_so=1'bz;
    integer i,beats=0,edges=0,offset,write_case;
    reg [31:0] random_state=32'h513bb431;
    realtime last_high=0,last_low=0,last_cs_high=0,last_cs_low=0,last_data=0;
`ifdef UJ11_VENDOR_IO
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    initial forever begin #(LOW_NS) clk=1; #(HIGH_NS) clk=0; end
    assign #(SCK_NS) pin_sck=sck;
    assign #(DATA_NS) pin_mosi=mosi;
    assign #(CS_NS) pin_cs=cs;
    assign #(RETURN_NS) miso=fram_so;
    uj11_board_fram dut(clk,rst,req,writing,byte_access,bank,keep_read,close_read,
        address,data,value,ready,error,busy,cs,sck,mosi,miso);
    spi_fram_model memory(pin_cs,pin_sck,pin_mosi,ideal_so);
    always @(negedge pin_sck or posedge pin_cs)begin
        if(pin_cs)fram_so=1'bz;
        else begin
            fram_so=1'bx;
            #(VALID_NS+0.001);fram_so=pin_cs ? 1'bz : ideal_so;
        end
    end
    always @(posedge pin_sck)begin
        if(!rst)begin
            if(pin_cs || $realtime-last_cs_low<10.0 || $realtime-last_low<13.0)
                $fatal(1,"FRAM SCK/CS setup timing");
            if($realtime-last_data<5.0)$fatal(1,"MOSI setup timing");
        end
        if(!pin_cs)edges=edges+1;
        last_high=$realtime;
    end
    always @(negedge pin_sck)begin
        if(!rst && $realtime-last_high<13.0)$fatal(1,"SCK high timing");
        last_low=$realtime;
    end
    always @(pin_mosi)begin
        if(!rst && !pin_cs && $realtime-last_high<5.0)$fatal(1,"MOSI hold timing");
        last_data=$realtime;
    end
    always @(negedge pin_cs)begin
        if(!rst && $realtime-last_cs_high<10.0)$fatal(1,"CS deselect timing");
        last_cs_low=$realtime;
    end
    always @(posedge pin_cs)begin
        if(!rst && $realtime-last_high<10.0)$fatal(1,"CS hold timing");
        last_cs_high=$realtime;
    end
    task beat(input bit wr,byt,bnk,input [15:0] addr,datum,expected,input integer bits);
        integer before_edges,guard;
        begin
            @(negedge clk);req=1;writing=wr;byte_access=byt;bank=bnk;address=addr;data=datum;
            before_edges=edges;guard=0;
            while(!ready)begin
                @(negedge clk);guard=guard+1;
                if(guard>160)$fatal(1,"timed transaction timeout");
            end
            if(error!==(!byt && addr[0]))$fatal(1,"odd error");
            if(!wr && !error && value!==expected)
                $fatal(1,"timed MISO read %h got%h expected%h",addr,value,expected);
            if(edges-before_edges!=bits)$fatal(1,"unexpected pulse count %0d != %0d",edges-before_edges,bits);
            repeat(4)begin @(negedge clk);if(ready || busy || sck)$fatal(1,"held request repeated");end
            req=0;repeat(3)@(negedge clk);beats=beats+1;
        end
    endtask
    initial begin
        $display("CP56 oscillator high=%0.5f low=%0.5f ns",HIGH_NS,LOW_NS);
        for(i=0;i<131072;i=i+1)memory.memory[i]=8'(i^(i>>8)^(i>>16));
        repeat(12)@(negedge clk);rst=0;
        beat(0,0,0,16'h1000,0,16'h1110,48);
        for(i=1;i<128;i=i+1)
            beat(0,0,0,16'(4096+2*i),0,{memory.memory[4097+2*i],memory.memory[4096+2*i]},16);
        for(i=0;i<128;i=i+1)begin
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            beat(1,0,i[0],{random_state[15:1],1'b0},random_state[31:16],0,56);
            beat(0,0,i[0],{random_state[15:1],1'b0},0,random_state[31:16],48);
        end
        beat(1,1,0,16'h3001,16'h00a7,0,48);
        beat(0,1,0,16'h3001,0,16'h00a7,40);
        beat(0,0,0,16'h3001,0,0,0);
        beat(0,0,0,16'hfffe,0,{memory.memory[65535],memory.memory[65534]},48);
        beat(0,0,0,16'h0000,0,{memory.memory[1],memory.memory[0]},48);
        // Unknown valid payload remains unknown, rather than being sanitized.
        memory.memory[16'h4000]=8'b10xz1101;memory.memory[16'h4001]=8'b0110zx01;
        beat(0,0,0,16'h4000,0,16'b0110zx01_10xz1101,48);
        // Reset in every quarter-cycle offset through READ and WREN/WRITE.
        // Interrupted writes may commit bytes; each following read checks the
        // independent model's actual committed contents, not a rollback promise.
        for(write_case=0;write_case<2;write_case=write_case+1)
        for(offset=0;offset<280;offset=offset+1)begin
            close_read=1;repeat(3)@(negedge clk);close_read=0;
            req=1;writing=1'(write_case);byte_access=0;bank=0;address=16'h5000;data=16'hb67a;
            #(HALF_NS*0.5*offset+HALF_NS*0.25);rst=1;req=0;
            repeat(4)@(negedge clk);
            if(!cs || sck || busy || ready)$fatal(1,"reset did not quiesce bus");
            rst=0;
            beat(0,0,0,16'h5000,0,{memory.memory[16'h5001],memory.memory[16'h5000]},48);
        end
        $display("PASS CP56 timed SPI: %0d beats, 560 reset offsets; CPU high=%0.5f low=%0.5f ns SCK/MOSI/CS=%0.1f/%0.1f/%0.1f ns MISO=%0.1f+%0.1f ns",beats,HIGH_NS,LOW_NS,SCK_NS,DATA_NS,CS_NS,VALID_NS,RETURN_NS);
        $finish;
    end
    initial begin #10000000;$fatal(1,"CP56 timed watchdog");end
endmodule
