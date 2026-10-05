`timescale 1ns/1ps
// Native 177130/177132 controller. Mechanics use the fixed system clock.
// CSR reads hold RPLY for 8 us so unmodified disk ROM CRC waits work in turbo.
module bk_fdd #(parameter integer CLOCK_HZ=24000000) (
    input wire clk, power_on, peripheral_reset, quiesce,
    input wire request, write, data_port,
    input wire [15:0] write_data,
    output reg ready=0,
    output reg [15:0] read_data=0,
    output wire idle,error,
    input wire [3:0] present,readonly,
    output wire sector_request,sector_write,
    output wire [1:0] sector_drive,
    output wire [10:0] sector_offset,
    input wire sector_ready,sector_done,sector_error,
    output wire [8:0] buffer_address,
    output wire buffer_write,
    output wire [7:0] buffer_data,
    input wire [7:0] buffer_read
);
    localparam WORD_CYCLES=CLOCK_HZ/15625, CSR_CYCLES=CLOCK_HZ/125000;
    reg [11:0] position=0,last_position=0;
    reg [$clog2(WORD_CYCLES)-1:0] word_counter=0;
    reg [1:0] word_pending=0;
    reg selected=0,motor=0,side=0,reset_pending=0;
    reg [1:0] drive=0;
    reg [6:0] heads[0:3];
    reg search=0,found=0,writing=0,data_ready=0,crc_ok=0;
    reg [15:0] data_latch=0,read_crc=16'hffff,write_crc=16'hffff,last_written=0;
    reg [1:0] write_age=0;
    wire cache_valid,cache_idle;
    wire [16:0] raw_word;
    reg word_write=0;
    reg [11:0] word_address=0;
    reg [16:0] word_value=0;
    reg bus_active=0,control_wait=0;
    reg [$clog2(CSR_CYCLES+1)-1:0] csr_delay=0;
    wire stopped=quiesce||reset_pending;
    wire accept=request&&!bus_active;
    wire consume=request&&!write&&data_port&&!bus_active;
    wire commit=accept&&write;
    wire tick=word_pending==1;
    reg next_selected;
    reg [1:0] next_drive;
    always @* begin
        next_selected=|write_data[3:0];
        next_drive=write_data[0]?0:write_data[1]?1:write_data[2]?2:3;
    end
    wire [15:0] status={motor&&cache_valid&&position<16,crc_ok&&!error,6'b0,data_ready,4'b0,
                        selected&&readonly[drive],motor&&cache_valid,selected&&heads[drive]==0};
    wire [15:0] swapped={write_data[7:0],write_data[15:8]};
    wire [15:0] crc_value=commit?(data_port?swapped:last_written):raw_word[15:0];
    wire [15:0] crc_old=commit?(data_port?write_crc:16'hffff):
                       (!found&&search&&raw_word[16]?16'hffff:read_crc);
    wire [15:0] next_crc,cache_crc_old,cache_crc_value;
    // Cache construction/flush and native rotation never own the track together.
    bk_crc16_word crc_datapath(cache_valid?crc_old:cache_crc_old,
                               cache_valid?crc_value:cache_crc_value,next_crc);
    assign idle=cache_idle&&!writing&&!word_write&&!bus_active;
    bk_fdd_track cache(clk,power_on,stopped,writing,selected,drive,heads[drive],side,present,readonly,
        cache_valid,cache_idle,error,position,raw_word,word_write,word_address,word_value,
        sector_request,sector_write,sector_drive,sector_offset,
        sector_ready,sector_done,sector_error,buffer_address,buffer_write,buffer_data,buffer_read,
        next_crc,cache_crc_old,cache_crc_value);
    wire defer_control = writing && (!next_selected || next_drive!=drive || write_data[5]!=side ||
                          write_data[7] || !write_data[4] || write_data[8]);
    wire apply_control = (accept && write && !data_port && !defer_control) ||
                         (control_wait && !writing && cache_idle);
    wire [6:0] stepped_head = heads[next_drive] + (write_data[6] ? 7'd1 : 7'h7f);
    integer head;
    always @(posedge clk) begin
        word_write<=0;
        if(power_on)begin
            position<=0;word_counter<=0;word_pending<=0;last_position<=0;
            selected<=0;motor<=0;side<=0;drive<=0;reset_pending<=0;
            for(head=0;head<4;head=head+1)heads[head]<=0;
            search<=0;found<=0;writing<=0;data_ready<=0;crc_ok<=0;write_age<=0;
            data_latch<=0;read_crc<=16'hffff;write_crc<=16'hffff;last_written<=0;
            bus_active<=0;control_wait<=0;ready<=0;read_data<=0;csr_delay<=0;
        end else begin
            if(peripheral_reset)reset_pending<=1;
            if(reset_pending&&cache_idle&&!writing&&!word_write)begin
                selected<=0;motor<=0;search<=0;found<=0;data_ready<=0;crc_ok<=0;reset_pending<=0;
            end
            if(word_counter==WORD_CYCLES-1)begin
                word_counter<=0;position<=position==3124?12'b0:position+1'b1;word_pending<=2;
            end else word_counter<=word_counter+1'b1;
            if(word_pending!=0)word_pending<=word_pending-1'b1;
            if(tick&&motor&&cache_valid)begin
                if(writing)begin
                    if(write_age!=3)write_age<=write_age+1'b1;
                    data_ready<=1;
                    if(write_age==1)begin
                        word_write<=1;word_address<=last_position==3124?12'b0:last_position+1'b1;
                        word_value<={1'b0,write_crc};crc_ok<=1;
                    end else if(write_age==2)begin writing<=0;data_ready<=0;end
                end else if(search&&!found)begin
                    if(raw_word[16])begin
                        found<=1;data_ready<=1;data_latch<=raw_word[15:0];read_crc<=next_crc;
                    end
                end else if(found||!search)begin
                    if(data_ready&&!consume)begin
                        if(search)begin search<=0;crc_ok<=read_crc==0;end
                    end else begin
                        data_ready<=1;data_latch<=raw_word[15:0];
                        if(search)read_crc<=next_crc;
                    end
                end
            end
            if(!cache_valid&&!writing)begin found<=0;data_ready<=0;end
            if(request&&!bus_active)begin
                bus_active<=1;
                if(!write&&!data_port)begin csr_delay<=CSR_CYCLES;ready<=0;end
                else begin
                    ready<=1;
                    if(!write)begin
                        read_data<=cache_valid&&motor?(data_ready?data_latch:raw_word[15:0]):16'b0;
                        data_ready<=tick&&motor&&cache_valid&&data_ready&&(found||!search);
                    end else if(data_port)begin
                        if(cache_valid&&motor&&!readonly[drive]&&!stopped)begin
                            writing<=1;search<=0;found<=0;data_ready<=0;crc_ok<=0;write_age<=0;
                            word_write<=1;word_address<=position;word_value<={1'b0,swapped};
                            last_position<=position;last_written<=swapped;write_crc<=next_crc;
                        end
                    end else begin
                        if(defer_control)begin
                            // Drain the old track before changing its physical head/owner.
                            control_wait<=1;ready<=0;
                        end
                    end
                end
            end else if(control_wait)begin
                if(!writing&&cache_idle)begin control_wait<=0;ready<=request;end
            end else if(csr_delay!=0)begin
                csr_delay<=csr_delay-1'b1;
                if(csr_delay==1)begin ready<=request;read_data<=selected?status:16'b0;end
            end else if(!request)begin ready<=0;bus_active<=0;end
            if(apply_control)begin
                        selected<=next_selected;drive<=next_drive;motor<=write_data[4];side<=write_data[5];
                if(next_selected && write_data[7] &&
                   (write_data[6] ? heads[next_drive]<79 : heads[next_drive]!=0))
                    heads[next_drive]<=stepped_head;
                        if(!next_selected||!write_data[4]||next_drive!=drive||write_data[5]!=side||write_data[7])begin
                            search<=0;found<=0;data_ready<=0;crc_ok<=0;
                        end
                        if(write_data[9]&&writing)begin
                            word_write<=1;word_address<=last_position;word_value<={1'b1,last_written};
                            write_crc<=next_crc;
                        end
                        if(write_data[8])begin search<=1;found<=0;data_ready<=0;crc_ok<=0;read_crc<=16'hffff;end
            end
        end
    end
endmodule
