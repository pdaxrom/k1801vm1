`timescale 1ns/1ps
// RK611 front end for the HC7000 I/O processor. Guest offsets are word indices;
// IOP offsets are 32-bit indices. IOP 16=status, 17=completion/error word.
module uj11_mmu_rk611(
    input wire clk, reset, bus_reset, request, write, storage_enabled,
    output reg [15:0] storage_status,
    input wire [3:0] address,
    input wire [1:0] lanes,
    input wire [15:0] wdata,
    output reg [15:0] rdata,
    output reg ready,
    input wire irq_ack,
    output reg irq, busy,
    output wire cancel,
    input wire iop_write,
    input wire [4:0] iop_address,
    input wire [31:0] iop_data,
    output wire iop_valid,enabled,
    output reg [31:0] iop_rdata
);
    reg seen;
    reg [31:0] epoch,claim;
    assign iop_valid=busy && epoch==claim;
    assign enabled=|present;
    reg [7:0] present,write_protected;
    reg [15:0] cs1,wc,ba,da,cs2,er,dc,db,mr;
    wire accept=request && !seen;
    wire [15:0] merged=(rdata & ~{{8{lanes[1]}},{8{lanes[0]}}}) |
                               (wdata & {{8{lanes[1]}},{8{lanes[0]}}});
    assign cancel=bus_reset || (accept && write &&
        ((address==0 && lanes[1] && wdata[15]) ||
         (address==4 && lanes[0] && wdata[5])));
    wire [15:0] drive_status=!storage_enabled ? 16'o100701 :
        !present[cs2[2:0]] ? 16'b0 : 16'o100701 | (write_protected[cs2[2:0]] ? 16'o4000 : 16'b0);
    wire [159:0] register_words={db,dc,mr,er,drive_status,cs2,da,ba,wc,cs1};
    function [15:0] value(input [3:0] a,input [159:0] words);
        value=a<10 ? words[16*a+:16] : 16'b0;
    endfunction
    always @* begin
        rdata=value(address,register_words);
        iop_rdata=iop_address==24 ? epoch : iop_address==25 ? {31'b0,iop_valid} :
            iop_address[4] ? {31'b0,busy} : {16'b0,value(iop_address[3:0],register_words)};
    end
    always @(posedge clk) begin
        ready<=0;
        if(reset || cancel) begin
            cs1<=16'o200;wc<=0;ba<=0;da<=0;cs2<=0;er<=0;dc<=0;db<=0;mr<=0;
            irq<=0;busy<=0;seen<=request;
            if(reset) begin epoch<=0;claim<=0;end else epoch<=epoch+1'b1;
            if(reset || !storage_enabled)begin present<=0;write_protected<=0;storage_status<=0;end
            ready<=cancel && !reset;
        end else begin
            if(!request) seen<=0;
            if(irq_ack) irq<=0;
            if(accept) begin
                seen<=1;ready<=1;
                if(write) case(address)
                    0: begin
                        if(lanes[0]) begin
                            cs1[6]<=wdata[6];
                            if(!wdata[6]) irq<=0;
                            else if(!busy && cs1[7]) irq<=1;
                            if(!busy && wdata[0]) begin
                                cs1<=(merged & 16'o003577) | 16'o1;
                                er<=0;cs2<=cs2 & 16'o7;busy<=1;irq<=0;epoch<=epoch+1'b1;
                            end
                        end
                        if(lanes[1] && !busy) cs1[9:8]<=wdata[9:8];
                    end
                    1:if(!busy)wc<=merged;
                    2:if(!busy)ba<=merged & 16'hfffe;
                    3:if(!busy)da<=merged;
                    4:if(!busy)cs2<=merged & 16'o7;
                    7:if(!busy)mr<=merged;
                    8:if(!busy)dc<=merged;
                    9:if(!busy)db<=merged;
                    default:begin end
                endcase
            end
            // Firmware updates transfer registers only after a completed sector.
            // Guest clear/reset above always wins over an old completion.
            if(iop_write && storage_enabled) case(iop_address)
                18:present<=iop_data[7:0];
                19:write_protected<=iop_data[7:0];
                20:storage_status<=iop_data[15:0];
                25:claim<=iop_data;
                default:begin end
            endcase
            if(iop_write && busy && (!storage_enabled || iop_valid)) case(iop_address)
                0:cs1[9:8]<=iop_data[9:8];
                1:wc<=iop_data; 2:ba<=iop_data; 3:da<=iop_data;
                4:cs2<=iop_data; 6:er<=iop_data; 8:dc<=iop_data; 9:db<=iop_data;
                17:begin
                    er<=iop_data;
                    cs1<=(cs1 & 16'o003576) | 16'o200 |
                        (iop_data!=0 || |cs2[15:8] ? 16'o100000 : 16'o0) |
                        (cs1[5:1]==5'o5 ? 16'o40000 : 16'o0);
                    busy<=0;
                    // A simultaneous IE write takes effect on this completion.
                    irq<=accept && write && address==0 && lanes[0] ? wdata[6] : cs1[6];
                end
                default:begin end
            endcase
        end
    end
endmodule
