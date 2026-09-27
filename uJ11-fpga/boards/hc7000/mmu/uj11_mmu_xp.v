`timescale 1ns/1ps
// RP/RM (XP) RM05 front end. DMA extension is 22-bit; no UNIBUS map is implied.
module uj11_mmu_xp(
    input wire clk,reset,bus_reset,request,write,
    input wire [4:0] address,
    input wire [1:0] lanes,
    input wire [15:0] wdata,
    output reg [15:0] rdata,
    output reg ready,irq,busy,
    input wire irq_ack,iop_write,
    input wire [5:0] iop_address,
    input wire [31:0] iop_data,
    output reg [31:0] iop_rdata,
    output wire iop_valid,enabled
);
    reg seen;
    reg [15:0] cs1,wc,ba,cs2,db;
    reg [5:0] bae;
    reg [15:0] da[0:7],dc[0:7],cc[0:7],of[0:7],er[0:7];
    reg [7:0] present,write_protected,volume_valid,attention,last_sector;
    reg [31:0] epoch,claim;
    wire [2:0] unit=cs2[2:0];
    wire accept=request && !seen;
    wire cancel=bus_reset || (accept && write && address==4 && lanes[0] && wdata[5]);
    wire [15:0] mask={{8{lanes[1]}},{8{lanes[0]}}};
    wire [15:0] merged=(rdata & ~mask) | (wdata & mask);
    wire [15:0] ds=present[unit] ? 16'o10400 | (busy ? 16'o0 : 16'o200) |
        (volume_valid[unit] ? 16'o100 : 16'o0) | (write_protected[unit] ? 16'o4000 : 16'o0) |
        (last_sector[unit] ? 16'o2000 : 16'o0) | (attention[unit] ? 16'o100000 : 16'o0) | (er[unit]!=0 ? 16'o40000 : 16'o0) : 16'b0;
    assign iop_valid=busy && epoch==claim;
    assign enabled=|present;
    function [15:0] value(input [4:0] a);
        case(a)
            0:value=(cs1 & ~16'o101400) | {6'b0,bae[1:0],8'b0} | 16'o4000 |
                (cs1[14] || |attention ? 16'o100000 : 16'o0);
            1:value=wc;2:value=ba;3:value=da[unit];4:value=cs2 | 16'o300;
            5:value=ds;6:value=er[unit];7:value={8'b0,attention};
            8:value={4'b0,da[unit][5:0],6'b0};9:value=db;
            11:value=16'o20027;12:value=16'o21+{13'b0,unit};
            13:value=of[unit];14:value=dc[unit];15:value=cc[unit];
            20:value={10'b0,bae};21:value=cs1 & 16'o100;
            default:value=0;
        endcase
    endfunction
    always @* begin
        rdata=value(address);
        iop_rdata=iop_address==32 ? {31'b0,busy} : iop_address==40 ? epoch :
            iop_address==41 ? {31'b0,iop_valid} : {16'b0,value(iop_address[4:0])};
    end
    integer n;
    always @(posedge clk) begin
        ready<=0;
        if(reset || cancel)begin
            cs1<=16'o200;wc<=0;ba<=0;cs2<=0;db<=0;bae<=0;
            busy<=0;irq<=0;seen<=request;ready<=cancel && !reset;
            if(reset)begin
                epoch<=0;claim<=0;present<=0;write_protected<=0;volume_valid<=0;attention<=0;last_sector<=0;
                for(n=0;n<8;n=n+1)begin da[n]<=0;dc[n]<=0;cc[n]<=0;of[n]<=0;er[n]<=0;end
            end else begin
                epoch<=epoch+1'b1;
                for(n=0;n<8;n=n+1)er[n]<=0;
            end
        end else begin
            if(!request)seen<=0;
            if(irq_ack)irq<=0;
            if(accept)begin
                seen<=1;ready<=1;
                if(write)case(address)
                    0:begin
                        if(lanes[0])begin
                            cs1[6]<=wdata[6];
                            // Rewriting an already enabled IE after acknowledging
                            // DONE must not manufacture another interrupt.
                            if(!wdata[6])irq<=0;else if(!cs1[6] && !busy && cs1[7])irq<=1;
                            if(!busy)begin
                                cs1<=(merged & 16'o176) | (cs1 & 16'o140200);
                                if(wdata[0])begin
                                    cs1<=merged & 16'o177;busy<=1;irq<=0;
                                    cs2<=cs2 & 16'o37;epoch<=epoch+1'b1;
                                end
                            end
                        end
                        if(lanes[1] && !busy)begin
                            bae[1:0]<=wdata[9:8];
                            if(wdata[14])begin cs1[15:14]<=0;cs2<=cs2 & 16'o37;end
                        end
                    end
                    1:if(!busy)wc<=merged;
                    2:if(!busy)ba<=merged & 16'hfffe;
                    3:if(!busy)da[unit]<=merged & 16'o37777;
                    4:if(!busy)cs2<=merged & 16'o37;
                    7:if(lanes[0])attention<=attention & ~wdata[7:0];
                    9:if(!busy)db<=merged;
                    13:if(!busy)of[unit]<=merged & ~16'o161400;
                    14:if(!busy)dc[unit]<=merged & 16'o1777;
                    20:if(!busy)bae<=merged[5:0];
                    21:if(lanes[0])begin cs1[6]<=wdata[6];if(!wdata[6])irq<=0;end
                    default:begin end
                endcase
            end
            if(iop_write)begin
                case(iop_address)
                    34:present<=iop_data[7:0];35:write_protected<=iop_data[7:0];41:claim<=iop_data;
                    default:begin end
                endcase
                if(iop_valid)case(iop_address)
                    1:wc<=iop_data[15:0];2:ba<=iop_data[15:0];3:da[unit]<=iop_data[15:0];
                    4:cs2<=iop_data[15:0];6:er[unit]<=iop_data[15:0];
                    5:begin volume_valid[unit]<=iop_data[6];attention[unit]<=iop_data[15];last_sector[unit]<=iop_data[10];end
                    13:of[unit]<=iop_data[15:0];14:dc[unit]<=iop_data[15:0];15:cc[unit]<=iop_data[15:0];
                    20:bae<=iop_data[5:0];
                    33:begin
                        er[unit]<=iop_data[15:0];busy<=0;
                        cs1<=(cs1 & 16'o176) | 16'o200 |
                            (iop_data[15:0]!=0 || |cs2[15:8] ? 16'o140000 : 16'o0) |
                            (attention[unit] ? 16'o100000 : 16'o0);
                        irq<=accept && write && address==0 && lanes[0] ? wdata[6] : cs1[6];
                    end
                    default:begin end
                endcase
            end
        end
    end
endmodule
