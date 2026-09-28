`timescale 1ns/1ps
// HC7000-only RK611 + RV32I service processor. The old PDP-11 SD byte port
// remains available for boot/modules; ownership changes only at idle CS-high.
module uj11_mmu_disk #(
    parameter integer CLOCK_HZ=24000000, SD_SLOW_DIV=60, SD_FAST_DIV=2
)(
    input wire clk, reset, bus_reset,
    input wire rk_request, rk_write,
    input wire [3:0] rk_address,
    input wire [1:0] rk_lanes,
    input wire [15:0] rk_wdata,
    output wire [15:0] rk_rdata,
    output wire rk_ready, rk_irq,
    output wire storage_enabled,
    output wire [15:0] storage_status,
    input wire rk_irq_ack,
    input wire rl_request,xp_request,rl_irq_ack,xp_irq_ack,
    input wire [4:0] storage_address,
    output wire [15:0] rl_rdata,xp_rdata,
    output wire rl_ready,xp_ready,rl_irq,xp_irq,rh_enabled,rl_enabled,xp_enabled,
    input wire sd_request, sd_write, sd_byte,
    input wire [1:0] sd_address,
    input wire [15:0] sd_wdata,
    output wire [15:0] sd_rdata,
    output wire sd_ready, sd_error,
    output wire sd_cs_n, sd_sck, sd_mosi,
    input wire sd_miso,
    output wire dma_request, dma_write,dma_unibus,
    output wire [21:0] dma_address,
    output wire [15:0] dma_data,
    input wire dma_ready,dma_error,
    input wire [15:0] dma_rdata
);
    wire rk_busy,rl_busy,xp_busy,cancel,rk_valid,rl_valid,xp_valid;
    wire local_reset=reset || (bus_reset && !storage_enabled);
    wire iop_reset=local_reset || (cancel && !storage_enabled);
    reg servicing;reg [1:0] service_kind;
    assign dma_unibus=service_kind!=2; // RH11/RL11 use UNIBUS; RM05 uses 22-bit RH70 DMA.
    wire service_valid=service_kind==0 ? rk_valid : service_kind==1 ? rl_valid : xp_valid;
    wire pending=rk_busy || (storage_enabled && (rl_busy || xp_busy));
    wire [31:0] ia,da,dw,dr,mem_data;
    wire [3:0] ds;
    wire ic,dc,de;
    reg ia_ack,da_ack,dseen;
    wire memory_selected=storage_enabled ? da[31:13]==0 : da[31:11]==0;
    wire io_selected=da[31:11]==21'h80000;
    wire csr_selected=io_selected && da[10:8]==0;
    wire rl_selected=io_selected && da[10:8]==4 && storage_enabled;
    wire xp_selected=io_selected && da[10:8]==5 && storage_enabled;
    wire spi_selected=io_selected && da[10:8]==1;
    wire engine_selected=io_selected && da[10:8]==2;
    wire timer_selected=io_selected && da[10:8]==3;
    wire data_accept=dc && !dseen;
    wire [31:0] csr_data,rl_csr_data,xp_csr_data;
    wire [31:0] engine_data;
    reg owner;
    wire engine_busy,engine_spi_req,engine_spi_write;
    wire [7:0] engine_spi_data;
    wire [15:0] spi_rdata;
    wire spi_ready,spi_error,spi_busy;
    wire iop_spi_req=data_accept && spi_selected && owner && !engine_busy;
    wire iop_spi_done=owner && !engine_busy && spi_ready;
    wire engine_spi_done=owner && engine_busy && spi_ready;

    uj11_mmu_service_cpu cpu(
        .clk(clk),.i_rst(iop_reset),.i_timer_irq(1'b0),
        .o_ibus_adr(ia),.o_ibus_cyc(ic),.i_ibus_rdt(mem_data),.i_ibus_ack(ia_ack),
        .o_dbus_adr(da),.o_dbus_dat(dw),.o_dbus_sel(ds),.o_dbus_we(de),.o_dbus_cyc(dc),
        .i_dbus_rdt(dr),.i_dbus_ack(da_ack));

    uj11_mmu_iop_ram ram(.clk(clk),.enable(ic || (dc && memory_selected)),
        .address(dc && memory_selected ? da[12:2] : ia[12:2]),
        .write_enable(ds & {4{data_accept && memory_selected && de && !iop_reset}}),
        .write_data(dw),.data(mem_data),.storage_enabled(storage_enabled));
    always @(posedge clk) begin
        if(iop_reset) begin ia_ack<=0;da_ack<=0;dseen<=0;end
        else begin
            ia_ack<=ic && !ia_ack && !(dc && memory_selected) && (storage_enabled ? ia[31:13]==0 : ia[31:11]==0);
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
        if(local_reset) begin ms_divider<=0;milliseconds<=0;end
        else if(ms_divider==MS_DIV-1) begin ms_divider<=0;milliseconds<=milliseconds+1'b1;end
        else ms_divider<=ms_divider+1'b1;
    end
    assign dr=memory_selected ? mem_data :
        ({32{csr_selected}} & (da[6:2]==16 ? {30'b0,owner,rk_busy} : csr_data)) |
        ({32{rl_selected}} & (da[6:2]==16 ? {30'b0,owner,rl_busy} : rl_csr_data)) |
        ({32{xp_selected}} & (da[7:2]==32 ? {30'b0,owner,xp_busy} : xp_csr_data)) |
        ({32{spi_selected}} & {16'b0,spi_rdata}) |
        ({32{engine_selected}} & engine_data) |
        ({32{timer_selected}} & {16'b0,milliseconds});

    uj11_mmu_rk611 registers(.clk(clk),.reset(local_reset),.bus_reset(bus_reset),.storage_enabled(storage_enabled),.storage_status(storage_status),.request(rk_request),.write(rk_write),
        .address(rk_address),.lanes(rk_lanes),.wdata(rk_wdata),.rdata(rk_rdata),.ready(rk_ready),
        .irq_ack(rk_irq_ack),.irq(rk_irq),.busy(rk_busy),.cancel(cancel),
        .iop_write(data_accept && csr_selected && de && ds[0]),
        .iop_address(da[6:2]),.iop_data(dw),.iop_rdata(csr_data),.iop_valid(rk_valid),.enabled(rh_enabled));
    uj11_mmu_rl11 rl_registers(.clk(clk),.reset(local_reset),.bus_reset(bus_reset),
        .request(rl_request),.write(rk_write),.address(storage_address[2:0]),.lanes(rk_lanes),.wdata(rk_wdata),
        .rdata(rl_rdata),.ready(rl_ready),.irq(rl_irq),.irq_ack(rl_irq_ack),.busy(rl_busy),.enabled(rl_enabled),
        .iop_write(data_accept && rl_selected && de && ds[0]),.iop_address(da[6:2]),.iop_data(dw),
        .iop_rdata(rl_csr_data),.iop_valid(rl_valid));
    uj11_mmu_xp xp_registers(.clk(clk),.reset(local_reset),.bus_reset(bus_reset),
        .request(xp_request),.write(rk_write),.address(storage_address),.lanes(rk_lanes),.wdata(rk_wdata),
        .rdata(xp_rdata),.ready(xp_ready),.irq(xp_irq),.irq_ack(xp_irq_ack),.busy(xp_busy),.enabled(xp_enabled),
        .iop_write(data_accept && xp_selected && de && ds[0]),.iop_address(da[7:2]),.iop_data(dw),
        .iop_rdata(xp_csr_data),.iop_valid(xp_valid));
    always @(posedge clk)begin
        if(iop_reset)begin servicing<=0;service_kind<=0;end
        else if(data_accept && de && ds[0])begin
            if((csr_selected || rl_selected) && da[6:2]==25 || xp_selected && da[7:2]==41)begin
                servicing<=1;service_kind<=csr_selected ? 0 : rl_selected ? 1 : 2;
            end
            if((csr_selected || rl_selected) && da[6:2]==17 || xp_selected && da[7:2]==33)servicing<=0;
        end
    end
    uj11_mmu_sector_engine engine(.clk(clk),.reset(iop_reset),
        .reg_write(data_accept && engine_selected && de && ds[0]),
        .abort(storage_enabled && servicing && !service_valid),
        .reg_address(da[4:2]),.reg_data(dw),.reg_rdata(engine_data),.busy(engine_busy),
        .spi_request(engine_spi_req),.spi_write(engine_spi_write),.spi_data(engine_spi_data),
        .spi_ready(engine_spi_done),.spi_rdata(spi_rdata[7:0]),
        .dma_request(dma_request),.dma_write(dma_write),.dma_address(dma_address),
        .dma_data(dma_data),.dma_ready(dma_ready),.dma_error(dma_error),.dma_rdata(dma_rdata));
    always @(posedge clk) begin
        if(iop_reset) owner<=0;
        else if(!owner && (pending || servicing || (storage_enabled && !storage_status[15])) && !sd_request && !spi_busy && sd_cs_n)owner<=1;
        else if(owner && !pending && !servicing && (!storage_enabled || storage_status[15]) && !engine_busy && !spi_busy && sd_cs_n)owner<=0;
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
