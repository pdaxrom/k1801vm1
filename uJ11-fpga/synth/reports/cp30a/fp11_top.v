`timescale 1ns/1ps
module uj11_hc1200_microcomp(
    output wire sd_cs_n, sd_sck, sd_mosi, input wire sd_miso,
    input wire res, rx, output wire tx,
    output wire gpio_mosi, input wire gpio_miso,
    output wire gpio_msck, gpio_mcs, gpio_din, gpio_ce, gpio_clk,
    output wire gpio_rs, gpio_blank, gpio_reg_latch, inout wire [3:0] gpio_key_row
);
    wire clk;
    reg [1:0] reset_sync=2'b11;
    always @(posedge clk or negedge res) begin
        if(!res) reset_sync<=2'b11;
        else reset_sync<={1'b0,reset_sync[1]};
    end
    OSCH #(.NOM_FREQ("29.56")) oscillator(.STDBY(1'b0),.OSC(clk));
    wire host_miso, host_miso_oe;
    assign gpio_key_row[3]=host_miso_oe ? host_miso : 1'bz;
    uj11_board #(.FP11_CONTROL(1)) system(.clk(clk),.reset(reset_sync[0]),.uart_rx(rx),.uart_tx(tx),
        .panel_keys(gpio_key_row),.panel_din(gpio_din),.panel_ce(gpio_ce),.panel_clk(gpio_clk),
        .panel_rs(gpio_rs),.panel_blank(gpio_blank),.panel_latch(gpio_reg_latch),
        .host_miso(host_miso),.host_miso_oe(host_miso_oe),
        .fram_cs_n(gpio_mcs),.fram_sck(gpio_msck),.fram_mosi(gpio_mosi),.fram_miso(gpio_miso),
        .sd_cs_n(sd_cs_n),.sd_sck(sd_sck),.sd_mosi(sd_mosi),.sd_miso(sd_miso),
        .boot_complete(),.stopped());
endmodule
