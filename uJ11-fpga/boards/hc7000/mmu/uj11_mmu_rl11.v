`timescale 1ns/1ps
// RL11/RLV12 register front end. SERV owns command execution and head position.
module uj11_mmu_rl11(
    input wire clk,reset,bus_reset,request,write,
    input wire [2:0] address,
    input wire [1:0] lanes,
    input wire [15:0] wdata,
    output reg [15:0] rdata,
    output reg ready,irq,busy,
    input wire irq_ack,iop_write,
    input wire [4:0] iop_address,
    input wire [31:0] iop_data,
    output reg [31:0] iop_rdata,
    output wire iop_valid,enabled
);
    reg seen;
    reg [15:0] cs,ba,da,mp,header_crc,read_mp;
    reg [5:0] bae;
    reg [1:0] header_phase;
    reg [3:0] present;
    reg [31:0] epoch,claim;
    wire accept=request && !seen;
    wire [15:0] live_cs=(cs & ~16'o61) | {10'b0,bae[1:0],4'b0} |
        (present[cs[9:8]] && !busy ? 16'o1 : 16'o0);
    wire [15:0] mask={{8{lanes[1]}},{8{lanes[0]}}};
    wire [15:0] merged=(rdata & ~mask) | (wdata & mask);
    assign enabled=|present;
    assign iop_valid=busy && epoch==claim;
    function [15:0] value(input [2:0] a);
        case(a)
            0:value=live_cs;1:value=ba;2:value=da;3:value=mp;4:value={10'b0,bae};
            default:value=0;
        endcase
    endfunction
    always @* begin
        rdata=address==3 && seen && !write ? read_mp : value(address);
        iop_rdata=iop_address==24 ? epoch : iop_address==25 ? {31'b0,iop_valid} :
            iop_address==16 ? {31'b0,busy} : {16'b0,value(iop_address[2:0])};
    end
    always @(posedge clk) begin
        ready<=0;
        if(reset || bus_reset)begin
            cs<=16'o200;ba<=0;da<=0;mp<=0;bae<=0;header_crc<=0;read_mp<=0;header_phase<=0;
            if(reset)begin epoch<=0;claim<=0;present<=0;end
            else epoch<=epoch+1'b1;
            seen<=request;busy<=0;irq<=0;
        end else begin
            if(!request)seen<=0;
            if(irq_ack)irq<=0;
            if(accept)begin
                seen<=1;ready<=1;
                if(!write && address==3)read_mp<=mp;
                if(!write && address==3 && lanes[1] && header_phase!=0)begin
                    header_phase<=header_phase+1'b1;
                    if(header_phase==1)mp<=0;
                    if(header_phase==2)mp<=header_crc;
                end
                if(write)case(address)
                    0:begin
                        if(lanes[0])begin
                            cs[6]<=wdata[6];
                            if(!wdata[6])irq<=0;else if(!busy && cs[7])irq<=1;
                            if(!busy)begin
                                cs<=(merged & 16'o1776) | 16'o200;
                                bae[1:0]<=merged[5:4];
                                if(!wdata[7])begin
                                    cs<=merged & 16'o1776 & ~16'o200;
                                    busy<=1;irq<=0;header_phase<=0;epoch<=epoch+1'b1;
                                end
                            end
                        end else if(lanes[1] && !busy)cs[9:8]<=wdata[9:8];
                    end
                    1:if(!busy)ba<=merged & 16'hfffe;
                    2:if(!busy)da<=merged;
                    3:if(!busy)begin mp<=merged;header_phase<=0;end
                    4:if(!busy)bae<=merged[5:0];
                    default:begin end
                endcase
            end
            if(iop_write)begin
                if(iop_address==18)present<=iop_data[3:0];
                if(iop_address==25)claim<=iop_data;
                if(iop_valid)case(iop_address)
                    1:ba<=iop_data[15:0];2:da<=iop_data[15:0];3:mp<=iop_data[15:0];
                    4:bae<=iop_data[5:0];5:header_crc<=iop_data[15:0];6:header_phase<=1;
                    17:begin
                        cs<=(cs & 16'o1776) | 16'o200 | iop_data[15:0];busy<=0;
                        irq<=accept && write && address==0 && lanes[0] ? wdata[6] : cs[6];
                    end
                    default:begin end
                endcase
            end
        end
    end
endmodule
