`timescale 1ns/1ps
// HC7000 SERV and shared peripheral bus. The legacy MMU profile retains RK611.
module uj11_mmu_disk #(
    parameter integer CLOCK_HZ=24000000, SD_SLOW_DIV=60, SD_FAST_DIV=2, VIDEO_ENABLE=0, TERMINAL_ENABLE=0
)(
    input wire clk,reset,bus_reset,dma_map_enabled,
    output wire video_reg_write,
    output wire [2:0] video_reg_address,
    output wire [15:0] video_reg_data,
    output wire [1:0] video_reg_lanes,
    input wire [15:0] video_reg_read,
    input wire rk_request,rk_write,
    input wire [3:0] rk_address,
    input wire [1:0] rk_lanes,
    input wire [15:0] rk_wdata,
    output wire [15:0] rk_rdata,
    output wire rk_ready,rk_irq,
    input wire rk_irq_ack,
    output wire storage_enabled,cpu_start,
    input wire io_request,
    input wire [12:0] io_address,
    output wire [15:0] io_rdata,
    output wire io_ready,io_error,io_irq,
    output wire [8:0] io_vector,
    input wire io_irq_ack,
    input wire sd_request,sd_write,sd_byte,
    input wire [1:0] sd_address,
    input wire [15:0] sd_wdata,
    output wire [15:0] sd_rdata,
    output wire sd_ready,sd_error,
    output wire sd_cs_n,sd_sck,sd_mosi,
    input wire sd_miso,
    output wire dma_request,dma_write,dma_unibus,
    output wire [1:0] dma_lanes,
    output wire dma_reserved,
    input wire mirror_push,
    input wire [7:0] mirror_data,
    output wire mirror_ready,
    output wire [21:0] dma_address,
    output wire [15:0] dma_data,
    input wire dma_ready,dma_error,
    input wire [15:0] dma_rdata
);
    wire rk_busy,cancel;
    wire local_reset=reset || (bus_reset && !storage_enabled);
    wire iop_reset=local_reset || (cancel && !storage_enabled);
    wire want_sd,software_unibus,abort_dma,working;
    wire pending=storage_enabled ? working : rk_busy;
    assign dma_unibus=!storage_enabled || software_unibus;
    wire [31:0] ia,da,dw,dr,mem_data;
    wire [3:0] ds;
    wire ic,dc,de,mem_ready;
    reg bootstrap_ready;
    assign cpu_start=!storage_enabled || bootstrap_ready;
    reg ia_ack,da_ack,dseen,instruction_pending;
    reg [31:0] instruction_data;
    always @(posedge clk)instruction_data<=mem_data;
    wire instruction_read=ic && !ia_ack && !instruction_pending && !(dc && memory_selected) &&
        (storage_enabled ? (ia[31:15]==0 && ia[14:10]<19) : ia[31:11]==0);
    wire memory_selected=storage_enabled ? (da[31:15]==0 && da[14:10]<19) : da[31:11]==0;
    // Break the RV32C aligner carry -> address bounds -> EBR enable path.
    // A held instruction request is qualified before issuing its RAM read;
    // data RAM retains priority. Other board profiles keep their old latency.
    localparam REGISTER_FETCH=CLOCK_HZ==50000000 && TERMINAL_ENABLE;
    reg fetch_requested;
    always @(posedge clk)begin
        if(iop_reset)fetch_requested<=0;
        else fetch_requested<=instruction_read;
    end
    // SERV holds the address through ACK, so its address bus needs no copy.
    wire instruction_fetch=REGISTER_FETCH ? fetch_requested && ic && !instruction_pending &&
        !ia_ack && !(dc && memory_selected) : instruction_read;
    wire io_selected=da[31:10]==22'h100000;
    wire csr_selected=io_selected && da[9:8]==0;
    wire spi_selected=io_selected && da[9:8]==1;
    wire engine_selected=io_selected && da[9:8]==2;
    wire timer_selected=io_selected && da[9:8]==3;
    wire data_accept=dc && !dseen;
    wire terminal_selected=TERMINAL_ENABLE && da[31:3]==29'h080000a0;
    wire external_selected=TERMINAL_ENABLE && da[31:21]==11'h300;
    wire external_done;
    wire [31:0] external_data,terminal_data;
    wire video_selected=VIDEO_ENABLE && da[31:5]==27'h2000020;
    assign video_reg_write=video_selected && data_accept && de && !iop_reset;
    assign video_reg_address=da[4:2];
    assign video_reg_data=dw[15:0];
    assign video_reg_lanes=dw[17:16];
    wire [31:0] csr_data,bridge_data,engine_data;
    reg owner;
    wire engine_busy,engine_spi_req,engine_spi_write;
    wire buffer_request,buffer_write;
    wire [7:0] buffer_address;
    wire [15:0] buffer_input,buffer_data;
    wire [7:0] engine_spi_data;
    wire [15:0] spi_rdata;
    wire spi_ready,spi_error,spi_busy;
    wire iop_spi_req=data_accept && spi_selected && owner && !engine_busy;
    wire iop_spi_done=owner && !engine_busy && spi_ready;
    wire engine_spi_done=owner && engine_busy && spi_ready;
    uj11_mmu_service_cpu cpu(
        .clk(clk),.i_rst(iop_reset),.i_timer_irq(1'b0),
        .o_ibus_adr(ia),.o_ibus_cyc(ic),.i_ibus_rdt(CLOCK_HZ==50000000 ? instruction_data : mem_data),.i_ibus_ack(ia_ack),
        .o_dbus_adr(da),.o_dbus_dat(dw),.o_dbus_sel(ds),.o_dbus_we(de),.o_dbus_cyc(dc),
        .i_dbus_rdt(dr),.i_dbus_ack(da_ack));
    uj11_mmu_iop_ram ram(.clk(clk),.reset(iop_reset),
        .enable(!iop_reset && (instruction_fetch || (data_accept && memory_selected))),
        .address(dc && memory_selected ? da[14:2] : ia[14:2]),
        .write_enable(ds & {4{data_accept && memory_selected && de && !iop_reset}}),
        .write_data(dw),.data(mem_data),.storage_enabled(storage_enabled),.ready(mem_ready),
        .sector_request(buffer_request),.sector_write(buffer_write),.sector_address(buffer_address),
        .sector_write_data(buffer_input),.sector_read_data(buffer_data));
    always @(posedge clk)begin
        if(iop_reset)bootstrap_ready<=0;
        else if(storage_enabled && data_accept && timer_selected && da[2] && de && ds[0] && dw[0])
            bootstrap_ready<=1;
    end
    always @(posedge clk)begin
        if(iop_reset)begin ia_ack<=0;da_ack<=0;dseen<=0;instruction_pending<=0;end
        else begin
            // Separate the EBR/bank mux from SERV's RV32C expansion at
            // 50 MHz. An idle cycle after each ACK lets the aligner change
            // address for the second half of a straddling instruction.
            ia_ack<=CLOCK_HZ==50000000 ? instruction_pending : instruction_read && mem_ready;
            instruction_pending<=CLOCK_HZ==50000000 && instruction_fetch && mem_ready;
            da_ack<=0;
            if(!dc)dseen<=0;
            if(data_accept && (!spi_selected || iop_spi_done) && (!memory_selected || mem_ready) &&
                (!external_selected || external_done))begin da_ack<=1;dseen<=1;end
        end
    end
    localparam integer MS_DIV=CLOCK_HZ/1000;
    reg [$clog2(MS_DIV)-1:0] ms_divider;
    reg [15:0] milliseconds;
    always @(posedge clk)begin
        if(local_reset)begin ms_divider<=0;milliseconds<=0;end
        else if(ms_divider==MS_DIV-1)begin ms_divider<=0;milliseconds<=milliseconds+1'b1;end
        else ms_divider<=ms_divider+1'b1;
    end
    assign dr=memory_selected ? mem_data :
        ({32{csr_selected}} & (storage_enabled ? bridge_data : da[6:2]==16 ? {30'b0,owner,rk_busy} : csr_data)) |
        ({32{spi_selected}} & {16'b0,spi_rdata}) |
        ({32{engine_selected}} & engine_data) |
        ({32{timer_selected}} & {16'b0,milliseconds}) |
        ({32{terminal_selected}} & terminal_data) |
        ({32{external_selected}} & external_data) |
        ({32{video_selected}} & {16'b0,video_reg_read});
    // Constant storage_enabled from the generated RAM prunes this entire legacy
    // register front end in the storage build, including its decode and muxes.
    uj11_mmu_rk611 registers(.clk(clk),.reset(local_reset),.bus_reset(bus_reset),.storage_enabled(1'b0),.storage_status(),
        .request(rk_request && !storage_enabled),.write(rk_write),.address(rk_address),.lanes(rk_lanes),
        .wdata(rk_wdata),.rdata(rk_rdata),.ready(rk_ready),.irq_ack(rk_irq_ack),.irq(rk_irq),.busy(rk_busy),.cancel(cancel),
        .iop_write(data_accept && csr_selected && de && ds[0] && !storage_enabled),
        .iop_address(da[6:2]),.iop_data(dw),.iop_rdata(csr_data),.iop_valid(),.enabled());
    uj11_mmu_iop_bus bridge(.clk(clk),.reset(reset),.bus_reset(bus_reset),.enabled(storage_enabled),
        .dma_map_enabled(dma_map_enabled),
        .request(io_request),.write(rk_write),.address(io_address),.lanes(rk_lanes),.wdata(rk_wdata),
        .rdata(io_rdata),.ready(io_ready),.error(io_error),.irq(io_irq),.vector(io_vector),.irq_ack(io_irq_ack),
        .owner(owner),.want_sd(want_sd),.dma_unibus(software_unibus),.abort_dma(abort_dma),.working(working),
        .iop_write(data_accept && csr_selected && de && ds[0]),.iop_address(da[4:2]),.iop_data(dw),.iop_rdata(bridge_data));
    wire engine_dma_request,engine_dma_write,engine_dma_ready;
    wire [21:0] engine_dma_address;
    wire [15:0] engine_dma_data;
    generate if(TERMINAL_ENABLE)begin: terminal
        wire enabled;
        wire [31:0] fifo_data;
        wire [4:0] fifo_level;
        reg [31:0] fifo_latched;
        always @(posedge clk)begin
            if(iop_reset)fifo_latched<=0;
            else if(data_accept && terminal_selected && !de && !da[2])fifo_latched<=fifo_data;
        end
        uj11_console_fifo fifo(.clk(clk),.reset(iop_reset),.push(mirror_push),.data(mirror_data),
            .pop(data_accept && terminal_selected && !de && !da[2]),
            .configure(data_accept && terminal_selected && de && da[2] && ds[0]),.enable(dw[0]),
            .ready(mirror_ready),.value(fifo_data),.enabled(enabled),.level(fifo_level));
        assign terminal_data=da[2] ? {26'b0,fifo_level,enabled} : (dseen ? fifo_latched : fifo_data);
        wire auxiliary_request,auxiliary_write,auxiliary_ready;
        wire [21:0] auxiliary_address;
        wire [15:0] auxiliary_data;
        wire [1:0] auxiliary_lanes;
        uj11_serv_sram external_ram(.clk(clk),.reset(iop_reset),.cycle(dc && external_selected),
            .write(de),.address(da[20:0]),.data(dw),.lanes(ds),.acknowledged(external_done),.result(external_data),
            .request(auxiliary_request),.writing(auxiliary_write),.physical(auxiliary_address),
            .output_data(auxiliary_data),.output_lanes(auxiliary_lanes),.ready(auxiliary_ready),.input_data(dma_rdata));
        // Retain an owner through READY and request withdrawal. A SERV access
        // must never replace an outstanding sector transfer's address/data.
        reg [1:0] owner;
        reg held_write;
        reg [21:0] held_address;
        reg [15:0] held_data;
        reg [1:0] held_lanes;
        always @(posedge clk)begin
            if(iop_reset)begin owner<=0;held_write<=0;held_address<=0;held_data<=0;held_lanes<=0;end
            else case(owner)
                0:if(engine_dma_request)begin
                    owner<=1;held_address<=engine_dma_address;held_data<=engine_dma_data;
                    held_write<=engine_dma_write;held_lanes<=3;
                end else if(auxiliary_request)begin
                    owner<=2;held_address<=auxiliary_address;held_data<=auxiliary_data;
                    held_write<=auxiliary_write;held_lanes<=auxiliary_lanes;
                end
                1:if(!engine_dma_request)owner<=0;
                2:if(!auxiliary_request && !dma_ready)owner<=0;
                default:owner<=0;
            endcase
        end
        assign dma_request=owner==1 ? engine_dma_request : owner==2 && auxiliary_request;
        assign dma_write=held_write;
        assign dma_address=held_address;
        assign dma_data=held_data;
        assign dma_lanes=held_lanes;
        assign dma_reserved=owner==2;
        assign auxiliary_ready=owner==2 && dma_ready;
        assign engine_dma_ready=owner==1 && dma_ready;
    end else begin: no_terminal
        assign mirror_ready=1,terminal_data=0,external_data=0,external_done=0;
        assign dma_request=engine_dma_request,dma_write=engine_dma_write,dma_address=engine_dma_address;
        assign dma_data=engine_dma_data,dma_lanes=3,dma_reserved=0,engine_dma_ready=dma_ready;
    end endgenerate
    uj11_mmu_sector_engine engine(.clk(clk),.reset(iop_reset),
        .reg_write(data_accept && engine_selected && de && ds[0]),.abort(storage_enabled && abort_dma),
        .reg_address(da[4:2]),.reg_data(dw),.reg_rdata(engine_data),.busy(engine_busy),
        .spi_request(engine_spi_req),.spi_write(engine_spi_write),.spi_data(engine_spi_data),
        .spi_ready(engine_spi_done),.spi_rdata(spi_rdata[7:0]),
        .dma_request(engine_dma_request),.dma_write(engine_dma_write),.dma_address(engine_dma_address),
        .dma_data(engine_dma_data),.dma_ready(engine_dma_ready),.dma_error(dma_error),.dma_rdata(dma_rdata),
        .buffer_request(buffer_request),.buffer_write(buffer_write),.buffer_address(buffer_address),
        .buffer_input(buffer_input),.buffer_data(buffer_data));
    always @(posedge clk)begin
        if(iop_reset)owner<=0;
        else if(!owner && (storage_enabled ? want_sd : rk_busy) && !sd_request && !spi_busy && sd_cs_n)owner<=1;
        else if(owner && !(storage_enabled ? want_sd : rk_busy) && !engine_busy && !spi_busy && sd_cs_n)owner<=0;
    end
    spi_byte_service #(.SLOW_DIV(SD_SLOW_DIV),.FAST_DIV(SD_FAST_DIV)) spi(
        .clk(clk),.rst(iop_reset),.req(owner ? (engine_spi_req || iop_spi_req) : sd_request),
        .write(owner ? (engine_busy ? engine_spi_write : de) : sd_write),
        .byte_access(owner ? 1'b0 : sd_byte),.address(owner ? (engine_busy ? 2'b0 : {da[2],1'b0}) : sd_address),
        .wdata(owner ? (engine_busy ? {8'b0,engine_spi_data} : dw[15:0]) : sd_wdata),
        .rdata(spi_rdata),.ready(spi_ready),.error(spi_error),.busy(spi_busy),
        .cs_n(sd_cs_n),.sck(sd_sck),.mosi(sd_mosi),.miso(sd_miso));
    assign sd_rdata=spi_rdata;
    assign sd_ready=spi_ready && !owner;
    assign sd_error=spi_error;
endmodule
