`timescale 1ns/1ps
`include "rtl/bk_model_layout.vh"
// Combinational CPU address map. Inputs are held through each bus transfer.
module bk_memory_map (
    input wire [2:0] model,
    input wire [15:0] bank_register,
    input wire [6:0] a16_mode,
    input wire [15:0] address,
    input wire writing,
    output reg responds,
    output reg [19:0] physical
);
    reg readonly;
    function [2:0] ram_page;
        input [2:0] code;
        begin
            case(code)
                0: ram_page = 1;
                1: ram_page = 5;
                2: ram_page = 2;
                3: ram_page = 3;
                4: ram_page = 4;
                5: ram_page = 7;
                6: ram_page = 0;
                7: ram_page = 6;
            endcase
        end
    endfunction
    always @* begin
        responds = model <= 4 && address < 16'o177600;
        physical = 0;
        readonly = 0;
        if(model >= 3) begin
            if(address < 16'o040000) physical = {4'b0,address};
            else if(address < 16'o100000)
                physical = {3'b0,ram_page(bank_register[14:12]),address[13:0]};
            else if(address < 16'o140000) begin
                if((bank_register & 16'o33) != 0) begin
                    readonly = 1;
                    case(bank_register & 16'o33)
                        1: physical = {6'h08,address[13:0]};
                        2: physical = {6'h09,address[13:0]};
                        default: responds = 0;
                    endcase
                end else physical = {3'b0,ram_page(bank_register[10:8]),address[13:0]};
            end else if(address < 16'o160000) begin
                physical = {7'h14,address[12:0]}; readonly = 1;
            end else begin
                physical = {7'h15,address[12:0]}; readonly = 1;
                if(model == 3 && address >= 16'o170000) responds = 0;
            end
        end else if(address < 16'o100000) physical = {4'b0,address};
        else if(model == 0) begin
            if(address >= 16'o120000 && a16_mode[3]) begin
                physical = {4'h2,1'b0,address[14:0]}; readonly = 1;
            end else case(address[15:12])
                8,9: if(a16_mode[6:4] == 7 || a16_mode[6:4] == 3 ||
                        a16_mode[6:4] == 6 || a16_mode[6:4] == 2) begin
                    physical = {7'h10,address[12:0]}; readonly = 1;
                end else begin
                    physical = {7'h18,address[12:0]};
                    readonly = address[15:12] == 8 && (a16_mode[6:4] == 4 || a16_mode[6:4] == 1);
                    if(a16_mode[6:4] == 0) responds = 0;
                end
                10,11: begin
                    physical = {7'h19,address[12:0]};
                    if(a16_mode[6:4] == 0 || a16_mode[6:4] == 1 ||
                       a16_mode[6:4] == 2 || a16_mode[6:4] == 6) responds = 0;
                end
                12,13: begin
                    physical = {7'h18,address[12:0]};
                    if(a16_mode[6:4] != 7 && a16_mode[6:4] != 3 &&
                       a16_mode[6:4] != 2 && a16_mode[6:4] != 0) responds = 0;
                end
                14,15: if(a16_mode[6:4] == 2 || a16_mode[6:4] == 0) begin
                    physical = {7'h19,address[12:0]};
                    if(address[15:12] == 15 && (a16_mode[6:4] == 2 || !writing)) responds = 0;
                end else begin
                    physical = {8'h28,address[11:0]}; readonly = 1;
                    if(a16_mode[6:4] == 1 || (address[15:12] == 15 && a16_mode[6:4] != 7)) responds = 0;
                end
            endcase
        end else if(address < 16'o120000) begin
            physical = {7'h10,address[12:0]}; readonly = 1;
        end else if(model == 1) begin
            readonly = 1;
            if(address < 16'o140000) physical = {7'h14,address[12:0]};
            else if(address >= 16'o160000) physical = {7'h15,address[12:0]};
            else responds = 0;
        end else begin
            physical = {4'h2,1'b0,address[14:0]}; readonly = 1;
        end
        if(writing && readonly) responds = 0;
    end
endmodule
