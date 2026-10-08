`timescale 1ns/1ps
module uj11_terak_bus #(
    parameter integer CLOCK_HZ=50000000,QX_MECHANICAL_SPEEDUP=1,SD_SLOW_DIV=125,SD_FAST_DIV=2,CLEAR_WORDS=1048576,DISPLAY_EBR=0,
    parameter integer RAM_BYTES=57344,BOOT_BIOS=0,RL_ENABLE=0
)(
    input wire clk,reset,cold_reset,power_on,peripheral_reset,video_clk,video_reset,
    input wire request,writing,byte_access,cpu_lock,
    input wire [21:0] address,
    input wire [15:0] write_data,
    output wire ready,error,
    output wire [15:0] read_data,
    output wire irq_valid,
    output wire [2:0] irq_priority,
    output wire [15:0] irq_vector,
    input wire irq_ack,
    inout wire ps2_clock,
    input wire ps2_data,
    output wire [5:0] tvout,
    output wire [1:0] audio,
    output wire memory_initialized,cpu_start,storage_idle,
    output reg boot_complete,
    output wire [19:0] sram_address,
    inout wire [15:0] sram_data,
    output wire sram_ce_n,sram_oe_n,sram_we_n,sram_lb_n,sram_ub_n,
    output wire sd_cs_n,sd_sck,sd_mosi,
    input wire sd_miso
);
    wire [1:0] lanes=byte_access ? address[0] ? 2'b10 : 2'b01 : 2'b11;
    wire [12:0] offset={address[12:1],1'b0};
    wire io_page=&address[21:13];
    wire ram_selected=address<RAM_BYTES;
    wire aperture=io_page && !offset[12];
    wire rom_selected=io_page && offset[12:9]==(13'o13000>>9);
    wire qx_selected=io_page && offset[12:2]==(13'o17000>>2);
    wire qb_selected=io_page && (offset[12:4]==(13'o11000>>4) || BOOT_BIOS && offset==13'o11020);
    // RLV12 adds its standard BAE register at 174410 for the 22-bit profile.
    // The existing 18-bit RLV11 profile still responds to four registers only.
    wire rl_selected=RL_ENABLE && io_page && (offset[12:3]==(13'o14400>>3) ||
        RAM_BYTES>262144 && offset==13'o14410);
    wire service_selected=qb_selected || rl_selected;
    wire service_reset=BOOT_BIOS ? cold_reset : reset;
    wire guest_bus_reset=peripheral_reset || (BOOT_BIOS && reset);
    wire console_selected=io_page && (offset[12:3]==(13'o17560>>3) ||
        offset==13'o17740 || offset==13'o17742 || offset==13'o17744 || offset==13'o17746);
    // RT-11's original bootstrap probes the page buffer with TST @#160000
    // before loading CHRSET.SYS. DATI must respond even on this byte aperture.
    wire bad_aperture=aperture && writing && !byte_access;
    wire bad_register=console_selected && byte_access && (offset==13'o17740 || offset==13'o17742);
    wire [15:0] gar,vir,vcr;
    wire memory_request,memory_write,memory_ready,cpu_ready,video_ready;
    wire [19:0] memory_address,video_address;
    wire [1:0] memory_lanes;
    wire [15:0] memory_data,ram_data;
    wire video_request;
    wire video_display,video_font,display_ready,display_initialized,sram_initialized;
    wire display_video_ready;
    wire [15:0] display_data,display_video_data;
    wire display_selected=DISPLAY_EBR && aperture;
    wire video_display_selected=DISPLAY_EBR && video_display;
    assign memory_initialized=sram_initialized && display_initialized;
    generate if(DISPLAY_EBR)begin: display_ebr
        uj11_terak_display_ram memory(.clk(clk),.reset(reset || peripheral_reset),.power_on(power_on),
            .initialized(display_initialized),.cpu_request(request && aperture && !bad_aperture),
            .cpu_write(writing),.cpu_font(vcr[7]),.cpu_address(address[11:0]),.cpu_data(write_data),
            .cpu_ready(display_ready),.cpu_read_data(display_data),
            .video_request(video_request && video_display),.video_font(video_font),
            .video_address({video_address[10:0],1'b0}),.video_ready(display_video_ready),.video_data(display_video_data));
    end else begin: display_sram
        assign display_initialized=1'b1,display_ready=1'b0,display_video_ready=1'b0;
        assign display_data=16'b0,display_video_data=16'b0;
    end endgenerate
    wire dma_request,dma_write,dma_ready,arb_dma_ready;
    wire [21:0] dma_address;
    wire [15:0] dma_data,qb_data;
    wire [1:0] dma_lanes;
    wire qb_ready,qb_error,qb_irq;
    wire [8:0] qb_vector;
    reg dma_latched,dma_error;
    reg [21:0] dma_target;
    // Cut the sector engine's index/subtract/address path before arbitration.
    // The engine holds request/address/data until ready and drops request in
    // its gap cycle; each DMA word gets one address-validation stage.
    always @(posedge clk)begin
        if(reset || peripheral_reset)begin dma_latched<=0;dma_error<=0;dma_target<=0;end
        else if(!dma_request)dma_latched<=0;
        else if(!dma_latched)begin
            dma_latched<=1;dma_target<=dma_address;dma_error<=dma_address>=RAM_BYTES;
        end
    end
    assign dma_ready=dma_request && dma_latched && (dma_error || arb_dma_ready);
    wire [19:0] cpu_address=aperture ? (vcr[7] ? 20'h08800 : 20'h08000)+{9'b0,address[11:1]} : address[20:1];
    uj11_video_arbiter #(.FAST_TURNAROUND(CLOCK_HZ==50000000)) arbiter(
        .clk(clk),.reset(reset || peripheral_reset),
        .cpu_request(request && (ram_selected || (!DISPLAY_EBR && aperture && !bad_aperture))),.cpu_write(writing),
        .cpu_lock(cpu_lock),.cpu_address(cpu_address),.cpu_lanes(lanes),.cpu_data(write_data),.cpu_ready(cpu_ready),
        .dma_request(dma_request && dma_latched && !dma_error),.dma_write(dma_write),.dma_address(dma_target),
        .dma_data(dma_data),.dma_lanes(dma_lanes),.dma_ready(arb_dma_ready),
        .video_request(video_request && !video_display_selected),.video_address(video_address),.video_ready(video_ready),
        .request(memory_request),.write(memory_write),.address(memory_address),.lanes(memory_lanes),
        .data(memory_data),.ready(memory_ready));
    uj11_sram #(.CLEAR_WORDS(CLEAR_WORDS),.FAST_RESPONSE(CLOCK_HZ==50000000)) memory(
        .clk(clk),.power_on(power_on),.reset(reset || peripheral_reset),.initialized(sram_initialized),
        .request(memory_request),.write(memory_write),.address(memory_address),.byte_enable(memory_lanes),
        .write_data(memory_data),.read_data(ram_data),.ready(memory_ready),
        .sram_address(sram_address),.sram_data(sram_data),.sram_ce_n(sram_ce_n),.sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n),.sram_lb_n(sram_lb_n),.sram_ub_n(sram_ub_n));
    wire [15:0] underruns;
    wire field_toggle;
    uj11_terak_video video(.clk(clk),.video_clk(video_clk),.reset(reset || video_reset),
        .gar(gar),.vir(vir),.vcr(vcr),.memory_request(video_request),.memory_address(video_address),
        .memory_display(video_display),.memory_font(video_font),
        .memory_ready(video_display_selected ? display_video_ready : video_ready),
        .memory_data(video_display_selected ? display_video_data : ram_data),
        .dac(tvout),.underruns(underruns),.field_toggle(field_toggle));
    wire service_write;
    wire [6:0] service_address;
    wire [31:0] service_data,service_read,qx_service_read;
    // Remember that a guest has run before: cold_reset clears CPU_START in
    // the SERV bridge before this clock edge. The latch survives SERV resets
    // but clears on power-on, so initial SRAM clearing cannot request Setup.
    reg setup_requested,boot_started;
    always @(posedge clk)begin
        if(power_on)begin setup_requested<=0;boot_started<=0;end
        else begin
            if(cpu_start)boot_started<=1;
            if(BOOT_BIOS && cold_reset && boot_started)setup_requested<=1;
            else if(service_write && service_address==3 && service_data[31])setup_requested<=0;
        end
    end
    assign service_read=qx_service_read | {BOOT_BIOS && setup_requested && service_address==3,31'b0};
    wire qx_ready,qx_error,qx_busy,qx_irq,boot_qb,boot_menu,boot_rl,disk_idle;
    assign storage_idle=disk_idle && !qx_busy;
    wire [15:0] qx_data;
    uj11_terak_qx #(.CLOCK_HZ(CLOCK_HZ),.MECHANICAL_SPEEDUP(QX_MECHANICAL_SPEEDUP)) qx(.clk(clk),.reset(service_reset),.bus_reset(guest_bus_reset),
        .request(request && qx_selected),.writing(writing),.data_register(address[1]),
        .write_data(write_data),.read_data(qx_data),.ready(qx_ready),.error(qx_error),.busy(qx_busy),.boot_qb(boot_qb),.boot_menu(boot_menu),.boot_rl(boot_rl),
        .irq_ack(irq_ack && irq_priority==4 && qx_irq),.irq(qx_irq),
        .service_write(service_write),.service_address(service_address),.service_data(service_data),.service_read(qx_service_read));
    wire key_valid,key_ready,console_irq;
    wire [7:0] key_data;
    wire [15:0] console_data,console_vector;
    uj11_terak_console #(.CLOCK_HZ(CLOCK_HZ)) console(.clk(clk),.reset(reset),.bus_reset(peripheral_reset),
        .request(request && console_selected && !bad_register),.writing(writing),.address(offset),
        .lanes(lanes),.write_data(write_data),.read_data(console_data),.gar(gar),.vir(vir),.vcr(vcr),
        .key_valid(key_valid),.key_data(key_data),.key_ready(key_ready),.irq(console_irq),.vector(console_vector),
        .irq_ack(irq_ack && irq_priority==4 && !qx_irq && !qb_irq),.audio(audio));
    // Native BEVNT follows the 60 Hz video field, with a two-FF toggle CDC.
    // Peripheral RESET clears the IRQ latch while continuing to track fields.
    reg [2:0] event_reset;
    always @(posedge clk or posedge reset or posedge video_reset)begin
        if(reset || video_reset)event_reset<=7;
        else event_reset<={event_reset[1:0],1'b0};
    end
    reg [1:0] field_sync /* synthesis syn_preserve=1 */;
    reg field_seen;
    wire tick=field_sync[1]!=field_seen;
    reg clock_pending;
    always @(posedge clk)begin
        if(event_reset[2])begin field_sync<=0;field_seen<=0;clock_pending<=0;end
        else begin
            field_sync<={field_sync[0],field_toggle};field_seen<=field_sync[1];
            if(peripheral_reset)clock_pending<=0;
            else begin
                if(irq_ack && irq_priority==6)clock_pending<=0;
                if(tick)clock_pending<=1;
            end
        end
    end
    assign irq_valid=clock_pending || qx_irq || qb_irq || console_irq;
    wire rl_interrupt=RL_ENABLE && qb_irq && qb_vector==9'o160;
    assign irq_priority=clock_pending ? 3'd6 : rl_interrupt ? 3'd5 : 3'd4;
    assign irq_vector=clock_pending ? 16'o100 : rl_interrupt ? 16'o160 :
        qx_irq ? 16'o250 : qb_irq ? {7'b0,qb_vector} : console_vector;
    uj11_mmu_disk #(.CLOCK_HZ(CLOCK_HZ),.SD_SLOW_DIV(SD_SLOW_DIV),.SD_FAST_DIV(SD_FAST_DIV),
        .TERAK_ENABLE(1),.KEYBOARD_ENABLE(1),.IOP_RAM_KIB(DISPLAY_EBR ? 15 : 19)) disk(
        .clk(clk),.reset(service_reset),.bus_reset(guest_bus_reset),.dma_map_enabled(1'b0),
        .ps2_clock(ps2_clock),.ps2_data(ps2_data),
        .video_reg_write(),.video_reg_address(),.video_reg_data(),.video_reg_lanes(),.video_reg_read(16'b0),
        .rk_request(1'b0),.rk_write(writing),.rk_address(4'b0),.rk_lanes(lanes),.rk_wdata(write_data),.rk_rdata(),
        .rk_ready(),.rk_irq(),.rk_irq_ack(1'b0),.storage_enabled(),.cpu_start(cpu_start),.storage_idle(disk_idle),
        .io_request(request && service_selected),.io_address(offset),.io_rdata(qb_data),.io_ready(qb_ready),
        .io_error(qb_error),.io_irq(qb_irq),.io_vector(qb_vector),
        .io_irq_ack(irq_ack && qb_irq && (irq_priority==5 || irq_priority==4 && !qx_irq)),
        .sd_request(1'b0),.sd_write(1'b0),.sd_byte(1'b0),.sd_address(2'b0),.sd_wdata(16'b0),
        .sd_rdata(),.sd_ready(),.sd_error(),.sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .dma_request(dma_request),.dma_write(dma_write),.dma_unibus(),.dma_lanes(dma_lanes),
        .dma_reserved(),.dma_address(dma_address),.dma_data(dma_data),
        .dma_ready(dma_ready),.dma_error(dma_error),.dma_rdata(ram_data),
        .mirror_push(1'b0),.mirror_data(8'b0),.mirror_ready(),
        .local_valid(key_valid),.local_data(key_data),.local_ready(key_ready),
        .service_write(service_write),.service_address(service_address),.service_data(service_data),.service_read(service_read));
    wire [15:0] rom_data;
    uj11_terak_boot_rom boot(.address(address[8:1]),.qb(boot_qb),.menu(boot_menu),.rl(RL_ENABLE && boot_rl),.data(rom_data));
    wire [15:0] rom_response;
    wire rom_ready;
    generate if(BOOT_BIOS)begin: registered_rom
        reg valid;
        reg [15:0] data;
        // The dynamic BIOS/native-ROM entry otherwise extends address -> ROM
        // -> instruction decode -> microstore into one 50 MHz timing path.
        always @(posedge clk)begin
            if(reset || peripheral_reset || !request || !rom_selected)valid<=0;
            else if(!valid)begin data<=rom_data;valid<=1;end
        end
        assign rom_response=data,rom_ready=valid;
    end else begin: direct_rom
        assign rom_response=rom_data,rom_ready=1'b1;
    end endgenerate
    assign read_data=display_selected ? display_data : ram_selected || aperture ? ram_data : rom_selected ? rom_response :
        qx_selected ? qx_data : service_selected ? qb_data : console_data;
    assign ready=request && (display_selected && !bad_aperture ? display_ready :
        (ram_selected || aperture && !bad_aperture) ? cpu_ready :
        qx_selected ? qx_ready : service_selected ? qb_ready : rom_selected ? rom_ready : 1'b1);
    assign error=request && (bad_aperture || bad_register || (rom_selected && (writing || qx_busy)) ||
        (qx_selected && qx_error) || (service_selected && qb_error) ||
        !(ram_selected || aperture || rom_selected || qx_selected || service_selected || console_selected));
    always @(posedge clk)begin
        if(reset)boot_complete<=0;
        else if(request && !writing && address==0)boot_complete<=1;
    end
endmodule
