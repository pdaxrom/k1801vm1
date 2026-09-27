`timescale 1ns/1ps
// HC7000-only RK611 + RV32I service processor. The old PDP-11 SD byte port
// remains available for boot/modules; ownership changes only at idle CS-high.
module uj11_disk #(
    parameter integer CLOCK_HZ=24000000, SD_SLOW_DIV=60, SD_FAST_DIV=2
)(
    input wire clk, reset,
    input wire rk_request, rk_write,
    input wire [3:0] rk_address,
    input wire [1:0] rk_lanes,
    input wire [15:0] rk_wdata,
    output wire [15:0] rk_rdata,
    output wire rk_ready, rk_irq,
    input wire rk_irq_ack,
    input wire sd_request, sd_write, sd_byte,
    input wire [1:0] sd_address,
    input wire [15:0] sd_wdata,
    output wire [15:0] sd_rdata,
    output wire sd_ready, sd_error,
    output wire sd_cs_n, sd_sck, sd_mosi,
    input wire sd_miso,
    output wire dma_request, dma_write,
    output wire [15:0] dma_address, dma_data,
    input wire dma_ready,
    input wire [15:0] dma_rdata
);
    wire rk_busy,cancel;
    wire iop_reset=reset || cancel;
    wire [31:0] ia,da,dw,dr,mem_data;
    wire [3:0] ds;
    wire ic,dc,de;
    reg ia_ack,da_ack,dseen;
    wire memory_selected=da[31:11]==0;
    wire io_selected=da[31:10]==22'h100000;
    wire csr_selected=io_selected && da[9:8]==0;
    wire spi_selected=io_selected && da[9:8]==1;
    wire engine_selected=io_selected && da[9:8]==2;
    wire timer_selected=io_selected && da[9:8]==3;
    wire data_accept=dc && !dseen;
    wire [15:0] csr_data;
    wire [31:0] engine_data;
    reg owner;
    wire engine_busy,engine_spi_req,engine_spi_write;
    wire [7:0] engine_spi_data;
    wire [15:0] spi_rdata;
    wire spi_ready,spi_error,spi_busy;
    wire iop_spi_req=data_accept && spi_selected && owner && !engine_busy;
    wire iop_spi_done=owner && !engine_busy && spi_ready;
    wire engine_spi_done=owner && engine_busy && spi_ready;

    serv_rf_top #(.WITH_CSR(0),.COMPRESSED(0),.MDU(0),.PRE_REGISTER(1),
        .RESET_STRATEGY("MINI")) cpu(
        .clk(clk),.i_rst(iop_reset),.i_timer_irq(1'b0),
        .o_ibus_adr(ia),.o_ibus_cyc(ic),.i_ibus_rdt(mem_data),.i_ibus_ack(ia_ack),
        .o_dbus_adr(da),.o_dbus_dat(dw),.o_dbus_sel(ds),.o_dbus_we(de),.o_dbus_cyc(dc),
        .i_dbus_rdt(dr),.i_dbus_ack(da_ack),
        .o_ext_rs1(),.o_ext_rs2(),.o_ext_funct3(),.i_ext_rd(32'b0),
        .i_ext_ready(1'b0),.o_mdu_valid());

    uj11_iop_ram ram(.clk(clk),.enable(ic || (dc && memory_selected)),
        .address(dc && memory_selected ? da[10:2] : ia[10:2]),
        .write_enable(ds & {4{data_accept && memory_selected && de && !iop_reset}}),
        .write_data(dw),.data(mem_data));
    always @(posedge clk) begin
        if(iop_reset) begin ia_ack<=0;da_ack<=0;dseen<=0;end
        else begin
            ia_ack<=ic && !ia_ack && !(dc && memory_selected) && ia[31:11]==0;
            da_ack<=0;
            if(!dc)dseen<=0;
            if(data_accept && (!spi_selected || iop_spi_done)) begin
                da_ack<=1;dseen<=1;
            end
        end
    end
    localparam integer MS_DIV=CLOCK_HZ/1000;
    reg [$clog2(MS_DIV)-1:0] ms_divider;
    reg [15:0] milliseconds;
    always @(posedge clk) begin
        if(reset) begin ms_divider<=0;milliseconds<=0;end
        else if(ms_divider==MS_DIV-1) begin ms_divider<=0;milliseconds<=milliseconds+1'b1;end
        else ms_divider<=ms_divider+1'b1;
    end
    assign dr=memory_selected ? mem_data :
        ({32{csr_selected}} & {16'b0,(da[6:2]==16 ? {14'b0,owner,rk_busy} : csr_data)}) |
        ({32{spi_selected}} & {16'b0,spi_rdata}) |
        ({32{engine_selected}} & engine_data) |
        ({32{timer_selected}} & {16'b0,milliseconds});

    uj11_rk611 registers(.clk(clk),.reset(reset),.request(rk_request),.write(rk_write),
        .address(rk_address),.lanes(rk_lanes),.wdata(rk_wdata),.rdata(rk_rdata),.ready(rk_ready),
        .irq_ack(rk_irq_ack),.irq(rk_irq),.busy(rk_busy),.cancel(cancel),
        .iop_write(data_accept && csr_selected && de && ds[0]),
        .iop_address(da[6:2]),.iop_data(dw[15:0]),.iop_rdata(csr_data));
    uj11_sector_engine engine(.clk(clk),.reset(iop_reset),
        .reg_write(data_accept && engine_selected && de && ds[0]),
        .reg_address(da[3:2]),.reg_data(dw),.reg_rdata(engine_data),.busy(engine_busy),
        .spi_request(engine_spi_req),.spi_write(engine_spi_write),.spi_data(engine_spi_data),
        .spi_ready(engine_spi_done),.spi_rdata(spi_rdata[7:0]),
        .dma_request(dma_request),.dma_write(dma_write),.dma_address(dma_address),
        .dma_data(dma_data),.dma_ready(dma_ready),.dma_rdata(dma_rdata));
    always @(posedge clk) begin
        if(iop_reset) owner<=0;
        else if(!owner && rk_busy && !sd_request && !spi_busy && sd_cs_n)owner<=1;
        else if(owner && !rk_busy && !engine_busy && !spi_busy && sd_cs_n)owner<=0;
    end
    spi_byte_service #(.SLOW_DIV(SD_SLOW_DIV),.FAST_DIV(SD_FAST_DIV)) spi(
        .clk(clk),.rst(iop_reset),
        .req(owner ? (engine_spi_req || iop_spi_req) : sd_request),
        .write(owner ? (engine_busy ? engine_spi_write : de) : sd_write),
        .byte_access(owner ? 1'b0 : sd_byte),
        .address(owner ? (engine_busy ? 2'b0 : {da[2],1'b0}) : sd_address),
        .wdata(owner ? (engine_busy ? {8'b0,engine_spi_data} : dw[15:0]) : sd_wdata),
        .rdata(spi_rdata),.ready(spi_ready),.error(spi_error),.busy(spi_busy),
        .cs_n(sd_cs_n),.sck(sd_sck),.mosi(sd_mosi),.miso(sd_miso));
    assign sd_rdata=spi_rdata;
    assign sd_ready=spi_ready && !owner;
    assign sd_error=spi_error;
endmodule
