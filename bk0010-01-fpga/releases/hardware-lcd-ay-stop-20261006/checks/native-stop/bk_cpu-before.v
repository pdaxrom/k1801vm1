`timescale 1ns/1ps
// Native VM1A wrapper. Its own CSR slave stays in the CPU clock domain.
module bk_cpu (
    input wire clk_p, clk_n, reset,
    input wire virq, stop,
    input wire [15:0] irq_vector, external_input, system_input,
    output wire irq_ack, peripheral_reset,
    output wire [15:0] address, write_data,
    output wire [1:0] lanes,
    output wire cycle, strobe, write,
    input wire [15:0] read_data,
    input wire acknowledge,
    output wire system_write, system_read, external_write
);
    reg [5:0] release_count = 0;
    always @(posedge clk_p or posedge reset) begin
        if (reset) release_count <= 0;
        else if (release_count != 63) release_count <= release_count + 1'b1;
    end
    wire dclo = reset || release_count < 16;
    wire aclo = reset || release_count < 32;
    wire [15:0] core_read, internal_data;
    wire internal_ack;
    wire [2:1] selected;
    wire internal_select = address[15:4] == 12'hffc;
    wire core_cycle, core_strobe;
    wire reading_system = internal_select && core_strobe && !write && address[3:1] == 3'b111;
    reg system_active = 0, system_flag = 0;
    always @(posedge clk_p) begin
        if(reset || !reading_system) system_active <= 0;
        else if(!system_active) begin system_active <= 1; system_flag <= system_input[2]; end
    end
    // 177716 bit 2 is cleared by its read side effect. The VM1 slave samples
    // reg16 repeatedly while STB is held, so retain the pre-read flag until
    // the transfer ends, as the physical input latch does.
    wire [15:0] reg16 = {system_input[15:3],system_active ? system_flag : system_input[2],system_input[1:0]};
    assign cycle = core_cycle && !internal_select;
    assign strobe = core_strobe && !internal_select;
    assign core_read = internal_select ? internal_data : read_data;
    assign system_write = selected[1] && write;
    assign system_read = selected[1] && !write;
    assign external_write = selected[2] && write;

    vm1_wb #(.VM1_CORE_MULG_VERSION(0)) core (
        .vm_clk_p(clk_p), .vm_clk_n(clk_n), .vm_clk_ena(1'b1), .vm_clk_tve(1'b1),
        .vm_clk_sp(1'b0), .vm_clk_slow(1'b0), .vm_pa(2'b0), .vm_init_in(1'b0),
        .vm_init_out(peripheral_reset), .vm_dclo(dclo), .vm_aclo(aclo),
        .vm_irq({2'b0, stop}), .vm_virq(virq), .wbm_gnt_i(1'b1), .wbm_ios_o(),
        .wbm_adr_o(address), .wbm_dat_o(write_data), .wbm_dat_i(core_read),
        .wbm_cyc_o(core_cycle), .wbm_we_o(write), .wbm_sel_o(lanes),
        .wbm_stb_o(core_strobe), .wbm_ack_i(internal_select ? internal_ack : acknowledge),
        .wbi_dat_i(irq_vector), .wbi_stb_o(irq_ack), .wbi_ack_i(irq_ack),
        .wbs_adr_i(address[3:0]), .wbs_dat_i(write_data), .wbs_dat_o(internal_data),
        .wbs_cyc_i(core_cycle && internal_select), .wbs_we_i(write),
        .wbs_stb_i(core_strobe && internal_select), .wbs_ack_o(internal_ack),
        .vm_reg14(external_input), .vm_reg16(reg16), .vm_sel(selected)
    );
endmodule
