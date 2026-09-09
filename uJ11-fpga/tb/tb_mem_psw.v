`timescale 1ns/1ps
module tb_mem_psw;
    reg clk=0, reset=1, enable=0;
    reg [1:0] update=0;
    reg [3:0] nzvc=0;
    reg [15:0] value=0;
    wire [15:0] psw;
    reg active=0, writing=0, byte_access=0,error=0;
    reg [15:0] address=0,data=0;
    reg [7:0] wait_states=0;
    wire ack,request,reading,write,mem_byte,complete;
    wire [15:0] addr,write_data,read_data;
    wire [1:0] fault;
    wire [31:0] transactions,writes;
    integer wait_index,byte_index,i;
    uj11_psw status(.*);
    uj11_mem dut(.active(active),.writing(writing),.byte_access(byte_access),
        .address(address),.data(data),.ack(ack),.error(error),.request(request),.read(reading),
        .write(write),.byte_word(mem_byte),.addr(addr),.write_data(write_data),.complete(complete),.fault(fault));
    uj11_ram ram(.clk(clk),.reset(reset),.request(request),.reading(reading),.writing(write),
        .byte_access(mem_byte),.addr(addr),.write_data(write_data),.wait_states(wait_states),
        .ack(ack),.read_data(read_data),.transactions(transactions),.writes(writes));
    always #5 clk=~clk;
    task tick; begin @(posedge clk); #1; end endtask
    task psw_step;
        input [1:0] mode;
        input [15:0] expected;
        begin
            @(negedge clk); update=mode; enable=1;
            tick;
            if(psw!==expected) $fatal(1,"PSW mode%0d result%h expected%h",mode,psw,expected);
        end
    endtask
    initial begin
        tick; if(psw!==16'o340)$fatal(1,"reset IPL"); @(negedge clk); reset=0;
        value=16'hf5a1; psw_step(3,16'hf5a1);
        nzvc=4'ha; psw_step(1,16'hf5ab); // MOV-style preserve carry
        nzvc=4'h4; psw_step(2,16'hf5a4);
        value=0; nzvc=0; psw_step(0,16'hf5a4);
        @(negedge clk); enable=0; update=3;
        tick; if(psw!==16'hf5a4) $fatal(1,"PSW stall");
        for(wait_index=0;wait_index<4;wait_index=wait_index+1) begin
            wait_states=wait_index[7:0];
            for(byte_index=0;byte_index<2;byte_index=byte_index+1) begin
                @(negedge clk); active=1; writing=1; byte_access=byte_index[0];
                address=16'h1000+{15'b0,byte_index[0]}; data=16'hb6a5;
                for(i=0;i<wait_index;i=i+1) begin
                    #1; if(complete) $fatal(1,"early complete"); tick;
                end
                #1; if(!ack || !complete || fault) $fatal(1,"write complete");
                tick;
                @(negedge clk); writing=0;
                // Consecutive request beats, changing direction after ack.
                for(i=0;i<wait_index;i=i+1) tick;
                #1;
                if(!ack || read_data!==(byte_index ? 16'h00a5 : 16'hb6a5)) $fatal(1,"RAM lane/read");
                tick;
                @(negedge clk); active=0;
                tick;
            end
        end
        @(negedge clk); active=1; byte_access=0; address=1;
        #1; if(request || !complete || fault!==1) $fatal(1,"odd word protection");
        active=0; #1; if(complete || fault) $fatal(1,"inactive fault");
        if(transactions!==16 || writes!==8) $fatal(1,"duplicate transaction");
        $display("PASS memory/PSW: word/byte lanes, back-to-back beats, 0..3 waits, odd address, flag masks");
        $finish;
    end
    initial begin #20000; $fatal(1,"memory timeout"); end
endmodule
