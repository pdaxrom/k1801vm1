`timescale 1ns/1ps
// One blocking peripheral bus transaction, interpreted entirely by SERV.
// The CPU holds address/data/lanes until ready. No controller CSRs live here.
module uj11_mmu_iop_bus(
    input wire clk,reset,bus_reset,enabled,dma_map_enabled,
    input wire request,write,
    input wire [12:0] address,
    input wire [1:0] lanes,
    input wire [15:0] wdata,
    output reg [15:0] rdata,
    output reg ready,error,
    input wire irq_ack,
    output reg irq,
    output reg [8:0] vector,
    input wire owner,
    output reg want_sd,dma_unibus,abort_dma,working,
    input wire iop_write,
    input wire [2:0] iop_address,
    input wire [31:0] iop_data,
    output reg [31:0] iop_rdata
);
    reg reset_pending,ack_pending;
    always @* case(iop_address)
        0:iop_rdata={15'b0,dma_map_enabled,1'b0,vector[8:2],4'b0,owner,ack_pending,reset_pending,enabled && request && !ready};
        1:iop_rdata={wdata,lanes,write,address};
        default:iop_rdata=0;
    endcase
    always @(posedge clk)begin
        if(reset || !enabled)begin
            ready<=0;error<=0;rdata<=0;reset_pending<=0;ack_pending<=0;
            irq<=0;vector<=0;want_sd<=0;dma_unibus<=0;abort_dma<=0;working<=0;
        end else begin
            if(!request)ready<=0;
            if(iop_write)case(iop_address)
                0:begin
                    if(iop_data[1])reset_pending<=0;
                    if(iop_data[2])ack_pending<=0;
                end
                2:if(request && !ready && !reset_pending && !bus_reset)begin
                    rdata<=iop_data[15:0];error<=iop_data[16];ready<=1;
                end
                3:if(!ack_pending)begin irq<=iop_data[16];vector<={iop_data[8:2],2'b0};end
                4:begin want_sd<=iop_data[0];dma_unibus<=iop_data[1];abort_dma<=iop_data[2];working<=iop_data[3];end
                default:begin end
            endcase
            // Acknowledge/reset wins over a simultaneous firmware write.
            if(irq_ack && irq)begin irq<=0;ack_pending<=1;vector<=vector;end
            if(bus_reset)begin
                ready<=0;reset_pending<=1;irq<=0;ack_pending<=0;abort_dma<=1;
            end
        end
    end
endmodule
