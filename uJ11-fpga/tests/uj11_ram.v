`timescale 1ns/1ps
// Simulation-only 64 KiB byte-addressed RAM and bus stability checker.
module uj11_ram (
    input wire clk, reset, request, reading, writing, byte_access,
    input wire [15:0] addr, write_data,
    input wire [7:0] wait_states,
    output wire ack,
    output wire [15:0] read_data,
    output reg [31:0] transactions, writes
);
    reg [7:0] bytes [0:65535];
    reg [7:0] waited=0;
    reg held=0;
    reg [34:0] previous;
    wire [15:0] next_byte=addr+16'd1;
    assign ack = request && waited>=wait_states;
    assign read_data = byte_access ? {8'b0,bytes[addr]} : {bytes[next_byte],bytes[addr]};
    always @(posedge clk) begin
        if(reset) begin waited<=0; held<=0; transactions<=0; writes<=0; end
        else begin
            if(held && (!request || previous!=={addr,write_data,reading,writing,byte_access}))
                $fatal(1,"memory request changed before ack");
            if(request && (reading==writing)) $fatal(1,"memory needs exactly one direction");
            held <= request && !ack;
            previous <= {addr,write_data,reading,writing,byte_access};
            if(!request || ack) waited<=0;
            else waited<=waited+8'd1;
            if(request && ack) begin
                transactions<=transactions+1;
                if(writing) begin
                    writes<=writes+1;
                    bytes[addr]<=write_data[7:0];
                    if(!byte_access) bytes[next_byte]<=write_data[15:8];
                end
            end
        end
    end
endmodule
