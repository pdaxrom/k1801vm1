`timescale 1ns/1ps
// The transport's rdata is the sole PF_DATA word. No new transfer may
// overwrite it while buffer_valid is set. I/O is strictly demand-only.
module uj11_prefetch #(
    parameter integer CLK_DIV=1,
    parameter PREFETCH=1'b1
) (
    input wire clk, reset, request, write, byte_access, stream, prefetch_enable,
    input wire [15:0] address, wdata,
    input wire pc_write, stopped,
    input wire [15:0] pc_data,
    output wire [15:0] rdata,
    output wire ack, error,
    output wire io_request, io_write, io_byte,
    output wire [15:0] io_address, io_wdata,
    input wire [15:0] io_rdata,
    input wire io_ack, io_error,
    output wire spi_cs_n, spi_sck, spi_mosi,
    input wire spi_miso
);
    localparam [1:0] IDLE=0, DEMAND=1, SPECULATIVE=2;
    reg [1:0] owner;
    reg prediction_state, buffer_state, buffer_error, redirected;
    wire prediction_valid=prediction_state && !redirected;
    wire buffer_valid=buffer_state && !redirected;
    reg [14:0] next_word;
    wire io_page=&address[15:13];
    wire stream_read=stream && !write && !io_page && !address[0];
    wire match_word=prediction_valid && stream_read && address[15:1]==next_word;
    wire ready, transport_error, busy;
    wire [15:0] transport_data;
    wire buffered=PREFETCH!=0 && match_word && buffer_valid;
    wire arriving=PREFETCH!=0 && match_word && owner==SPECULATIVE && ready;
    wire demand_done=owner==DEMAND && ready;
    assign ack=request && !reset && !stopped &&
               (io_page ? io_ack : (demand_done || buffered || arriving));
    assign rdata=io_page ? io_rdata : byte_access ? {8'b0,transport_data[7:0]} : transport_data;
    assign error=io_page ? io_error : (buffered ? buffer_error : transport_error);
    wire consumed=request && ack && stream_read && !error;
    // Capture the late ALU-to-PC comparison separately from valid-state
    // updates. Mask both valid flags immediately after the PC-write edge;
    // the following clock clears their stored state. Matching PC writes keep
    // the buffer, and FETCH's simultaneous PC increment is not a redirect.
    wire redirect=pc_write && !consumed && pc_data!={next_word,1'b0};
    wire discard=stopped || redirected || (request && !match_word);
    wire launch_demand=owner==IDLE && !ready && request && !io_page && !buffered && !stopped;
    wire launch_spec=PREFETCH!=0 && owner==IDLE && !ready && !request &&
                     prediction_valid && !buffer_valid && !stopped && !pc_write && prefetch_enable;
    wire launch=(launch_demand || launch_spec) && !reset;
    wire [14:0] following_word=address[15:1]+15'd1;

    assign io_request=request && io_page && !reset && !stopped;
    assign io_write=write;
    assign io_byte=byte_access;
    assign io_address=address;
    assign io_wdata=wdata;
    uj11_fram_transport #(.CLK_DIV(CLK_DIV)) transport(
        .clk(clk),.rst(reset),.req(launch),.write(launch_demand && write),
        .byte_access(launch_demand && byte_access && !stream_read),
        .keep_read(launch_spec || stream_read),
        .resume_read(launch_spec || match_word),
        // Keep the ALU-result PC comparison off the SPI start/CS path. The
        // prediction flag records a redirect now; close the parked read next
        // clock. No speculative launch is allowed on any PC-write edge.
        .close_read(!consumed && (stopped || !prediction_valid || (request && !match_word))),
        .address(launch_spec ? {next_word,1'b0} : address),.wdata(wdata),
        .rdata(transport_data),.ready(ready),.error(transport_error),.busy(busy),
        .spi_cs_n(spi_cs_n),.spi_sck(spi_sck),.spi_mosi(spi_mosi),.spi_miso(spi_miso));

    always @(posedge clk) begin
        if(reset) begin
            owner<=IDLE; prediction_state<=0; next_word<=0; redirected<=0;
            buffer_state<=0; buffer_error<=0;
        end else begin
            redirected<=redirect;
            if(ready) owner<=IDLE;
            if(launch) owner<=launch_spec ? SPECULATIVE : DEMAND;
            if(PREFETCH!=0 && owner==SPECULATIVE && ready && prediction_valid) begin
                buffer_state<=1; buffer_error<=transport_error;
            end
            if(discard) begin prediction_state<=0; buffer_state<=0; end
            if(consumed) begin
                next_word<=following_word;
                prediction_state<=!(&following_word[14:12]);
                buffer_state<=0;
            end
        end
    end
    wire unused_busy=busy;
endmodule
