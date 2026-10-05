`timescale 1ns/1ps
// Single-sector runtime host derived from Pyldin's sd_spi_block.v (vendor/pyldin).
// The native BIOS initializes the shared SD card and hands over SDSC/SDHC mode.
// Runtime transfers retain the upstream 512-byte buffer, CRC and busy checks.
module bk_sd_host #(parameter RUN_DIV=1) (
    input wire clk,reset,request,write,
    input wire [31:0] lba,
    output wire ready,initialized,
    output reg done=0,error=0,
    input wire [8:0] buffer_address,
    input wire buffer_write,
    input wire [7:0] buffer_data_in,
    output wire [7:0] buffer_data_out,
    output reg sd_cs=1,
    input wire init_valid,block_mode,
    output reg byte_start=0,
    output reg [7:0] byte_tx=8'hff,
    output wire [7:0] divider,
    input wire byte_busy,byte_done,
    input wire [7:0] byte_rx
);
    localparam POWER=0,IDLE=1,CMD_SEND=2,CMD_POLL=3,READ_TOKEN=4,READ_DATA=5,
        READ_CRC1=6,READ_CRC2=7,WRITE_TOKEN=8,WRITE_FETCH=9,WRITE_DATA=10,
        WRITE_CRC1=11,WRITE_CRC2=12,WRITE_RESPONSE=13,WRITE_BUSY=14,FINISH=15,FINISH_CLOCK=16;
    reg [4:0] state=POWER;
    reg waiting=0,operation_write=0,block_addressing=0;
    reg [31:0] command_address=0;
    reg [2:0] command_index=0;
    reg [15:0] timeout_count=0,crc=0;
    reg [7:0] received_crc=0;
    reg [8:0] index=0;
    (* syn_ramstyle="block_ram" *) reg [7:0] buffer[0:511];
    reg [7:0] buffer_q;
    assign initialized=state!=POWER;
    assign ready=state==IDLE;
    assign divider=RUN_DIV;
    assign buffer_data_out=buffer_q;
    wire receive_store=state==READ_DATA&&waiting&&byte_done;
    always @(posedge clk)begin
        if(receive_store || (buffer_write&&ready))
            buffer[receive_store?index:buffer_address]<=receive_store?byte_rx:buffer_data_in;
        buffer_q<=buffer[ready?buffer_address:index];
    end
    function [15:0] crc_byte;
        input [15:0] old;
        input [7:0] value;
        reg [15:0] c;
        integer bit_number;
        begin
            c=old^{value,8'b0};
            for(bit_number=0;bit_number<8;bit_number=bit_number+1)
                c=c[15]?(c<<1)^16'h1021:c<<1;
            crc_byte=c;
        end
    endfunction
    wire [15:0] next_crc=crc_byte(crc,operation_write?byte_tx:byte_rx);
    reg [7:0] command_byte;
    always @*begin
        case(command_index)
            0:command_byte=operation_write?8'h58:8'h51;
            1:command_byte=command_address[31:24];
            2:command_byte=command_address[23:16];
            3:command_byte=command_address[15:8];
            4:command_byte=command_address[7:0];
            default:command_byte=8'h01;
        endcase
    end
    task send_byte;
        input [7:0] value;
        begin byte_tx<=value;byte_start<=1;waiting<=1;end
    endtask
    task fail;
        begin error<=1;sd_cs<=1;state<=FINISH_CLOCK;end
    endtask
    always @(posedge clk)begin
        byte_start<=0;done<=0;
        if(byte_done)waiting<=0;
        if(reset)begin
            state<=POWER;waiting<=0;operation_write<=0;block_addressing<=0;
            command_address<=0;command_index<=0;timeout_count<=0;crc<=0;received_crc<=0;index<=0;
            done<=0;error<=0;sd_cs<=1;byte_start<=0;byte_tx<=8'hff;
        end else case(state)
            POWER:if(init_valid)begin block_addressing<=block_mode;state<=IDLE;end
            IDLE:begin
                error<=0;
                if(request&&!done)begin
                    if(!block_addressing&&lba[31:23]!=0)fail();
                    else begin
                        command_address<=block_addressing?lba:{lba[22:0],9'b0};
                        operation_write<=write;command_index<=0;timeout_count<=0;
                        sd_cs<=0;state<=CMD_SEND;
                    end
                end
            end
            CMD_SEND:if(!waiting)send_byte(command_byte);
                else if(byte_done)begin
                    if(command_index==5)state<=CMD_POLL;
                    else command_index<=command_index+1'b1;
                end
            CMD_POLL:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    if(!byte_rx[7])begin
                        if(byte_rx!=0)fail();
                        else begin timeout_count<=0;state<=operation_write?WRITE_TOKEN:READ_TOKEN;end
                    end else if(timeout_count==15)fail();
                    else timeout_count<=timeout_count+1'b1;
                end
            READ_TOKEN:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    if(byte_rx==8'hfe)begin index<=0;crc<=0;state<=READ_DATA;end
                    else if(byte_rx!=8'hff||timeout_count==16'hffff)fail();
                    else timeout_count<=timeout_count+1'b1;
                end
            READ_DATA:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    crc<=next_crc;
                    if(index==511)state<=READ_CRC1;else index<=index+1'b1;
                end
            READ_CRC1:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin received_crc<=byte_rx;state<=READ_CRC2;end
            READ_CRC2:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    if({received_crc,byte_rx}!=crc)fail();else state<=FINISH;
                end
            WRITE_TOKEN:if(!waiting)send_byte(8'hfe);
                else if(byte_done)begin index<=0;crc<=0;state<=WRITE_FETCH;end
            WRITE_FETCH:state<=WRITE_DATA;
            WRITE_DATA:if(!waiting)send_byte(buffer_q);
                else if(byte_done)begin
                    crc<=next_crc;
                    if(index==511)state<=WRITE_CRC1;
                    else begin index<=index+1'b1;state<=WRITE_FETCH;end
                end
            WRITE_CRC1:if(!waiting)send_byte(crc[15:8]);
                else if(byte_done)state<=WRITE_CRC2;
            WRITE_CRC2:if(!waiting)send_byte(crc[7:0]);
                else if(byte_done)begin state<=WRITE_RESPONSE;timeout_count<=0;end
            WRITE_RESPONSE:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    if(byte_rx[4:0]==5'b00101)begin state<=WRITE_BUSY;timeout_count<=0;end
                    else if(byte_rx!=8'hff||timeout_count==16'hffff)fail();
                    else timeout_count<=timeout_count+1'b1;
                end
            WRITE_BUSY:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin
                    if(byte_rx==8'hff)state<=FINISH;
                    else if(timeout_count==16'hffff)fail();
                    else timeout_count<=timeout_count+1'b1;
                end
            FINISH:begin sd_cs<=1;state<=FINISH_CLOCK;end
            FINISH_CLOCK:if(!waiting)send_byte(8'hff);
                else if(byte_done)begin done<=1;state<=IDLE;end
            default:fail();
        endcase
    end
endmodule
