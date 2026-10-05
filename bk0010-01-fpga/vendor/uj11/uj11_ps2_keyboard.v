`timescale 1ns/1ps
// Raw scan-code transport only. Key translation and terminal policy are SERV firmware.
module uj11_ps2_keyboard #(parameter integer CLOCK_HZ=50000000)(
    input wire clk,reset,
    inout wire ps2_clock,
    input wire ps2_data,
    input wire pop,
    output wire [8:0] value
);
    localparam integer US_DIV=CLOCK_HZ/1000000;
    reg [$clog2(US_DIV)-1:0] divider;
    wire tick=divider==US_DIV-1;
    reg [1:0] clock_sync,data_sync;
    reg [2:0] history;
    reg filtered;
    wire falling=tick && filtered && history==0;
    reg [3:0] bit_count;
    reg [7:0] shift;
    reg parity,bad;
    reg [9:0] timeout;
    wire expired=tick && bit_count!=0 && timeout==999;
    wire received=falling && !inhibit && bit_count==10;
    // An ordered 00 fault marker makes SERV discard partial prefixes/modifiers.
    wire [7:0] byte_value=expired || bad || !data_sync[1] ? 8'b0 : shift;
    reg [7:0] fifo[0:15] /* synthesis syn_ramstyle = "distributed" */;
    reg [3:0] head,tail;
    reg [4:0] level;
    wire take=pop && level!=0;
    wire put=(received || expired) && (level<16 || take);
    reg inhibit;
    reg [6:0] held_us;
    assign ps2_clock=inhibit ? 1'b0 : 1'bz;
    assign value=level==0 ? 9'b0 : {1'b1,fifo[head]};
    always @(posedge clk)begin
        if(reset)begin
            divider<=0;clock_sync<=3;data_sync<=3;history<=7;filtered<=1;
            bit_count<=0;shift<=0;parity<=0;bad<=0;timeout<=0;
            head<=0;tail<=0;level<=0;inhibit<=0;held_us<=0;
        end else begin
            clock_sync<={clock_sync[0],ps2_clock};
            data_sync<={data_sync[0],ps2_data};
            divider<=tick ? 0 : divider+1'b1;
            if(tick)begin
                history<={history[1:0],clock_sync[1]};
                if(history==0)filtered<=0;
                else if(history==7)filtered<=1;
                if(bit_count==0 || falling)timeout<=0;
                else timeout<=timeout+1'b1;
                // Stop only between frames, reserve two FIFO slots, hold >=100 us.
                if(inhibit)begin
                    if(held_us<100)held_us<=held_us+1'b1;
                    else if(level<14)inhibit<=0;
                end else if(level>=14 && bit_count==0 && filtered && clock_sync==3)begin
                    inhibit<=1;held_us<=0;
                end
            end
            if(expired)bit_count<=0;
            else if(falling && !inhibit)begin
                if(bit_count==0)begin
                    if(!data_sync[1])begin bit_count<=1;parity<=0;bad<=0;end
                end else if(bit_count<=8)begin
                    shift<={data_sync[1],shift[7:1]};
                    parity<=parity^data_sync[1];bit_count<=bit_count+1'b1;
                end else if(bit_count==9)begin
                    bad<=parity==data_sync[1];bit_count<=10;
                end else bit_count<=0;
            end
            if(put)begin fifo[tail]<=byte_value;tail<=tail+1'b1;end
            if(take)head<=head+1'b1;
            case({put,take})
                2'b10:level<=level+1'b1;
                2'b01:level<=level-1'b1;
                default:begin end
            endcase
        end
    end
endmodule
