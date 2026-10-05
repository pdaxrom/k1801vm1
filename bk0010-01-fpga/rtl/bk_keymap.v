`timescale 1ns/1ps
// Physical PS/2 transport is reused; this decoder emits native BK key codes.
module bk_keymap (
    input wire clk, reset,
    input wire scan_valid,
    input wire [7:0] scan_code,
    output wire scan_pop,
    output reg valid = 0,
    output reg [7:0] code = 0,
    input wire ready,
    output wire key_held,
    output reg stop = 0,
    input wire bios_mode
);
    reg extended = 0, released = 0;
    reg [2:0] pause_left = 0;
    reg [5:0] modifiers = 0;
    reg caps = 0;
    // Physical key state keeps advancing even when the guest leaves its single
    // character register unread. STOP and break codes must never wait for it.
    reg held=0,caps_down=0;
    reg [8:0] last_key=0;
    reg [1:0] decode_stage=0;
    wire [8:0] key_index={extended,scan_code};
    assign key_held=held;
    assign scan_pop = scan_valid && decode_stage==2 && (!bios_mode || !valid || ready);
    function [8:0] normal_key;
        input [7:0] scan;
        begin case(scan)
            8'h01: normal_key = 9'h10c;
            8'h03: normal_key = 9'h117;
            8'h04: normal_key = 9'h18b;
            8'h05: normal_key = 9'h181;
            8'h06: normal_key = 9'h103;
            8'h09: normal_key = 9'h10e;
            8'h0a: normal_key = 9'h180;
            8'h0b: normal_key = 9'h182;
            8'h0c: normal_key = 9'h116;
            8'h0d: normal_key = 9'h109;
            8'h0e: normal_key = 9'h160;
            8'h15: normal_key = 9'h171;
            8'h16: normal_key = 9'h131;
            8'h1a: normal_key = 9'h17a;
            8'h1b: normal_key = 9'h173;
            8'h1c: normal_key = 9'h161;
            8'h1d: normal_key = 9'h177;
            8'h1e: normal_key = 9'h132;
            8'h21: normal_key = 9'h163;
            8'h22: normal_key = 9'h178;
            8'h23: normal_key = 9'h164;
            8'h24: normal_key = 9'h165;
            8'h25: normal_key = 9'h134;
            8'h26: normal_key = 9'h133;
            8'h29: normal_key = 9'h120;
            8'h2a: normal_key = 9'h176;
            8'h2b: normal_key = 9'h166;
            8'h2c: normal_key = 9'h174;
            8'h2d: normal_key = 9'h172;
            8'h2e: normal_key = 9'h135;
            8'h31: normal_key = 9'h16e;
            8'h32: normal_key = 9'h162;
            8'h33: normal_key = 9'h168;
            8'h34: normal_key = 9'h167;
            8'h35: normal_key = 9'h179;
            8'h36: normal_key = 9'h136;
            8'h3a: normal_key = 9'h16d;
            8'h3b: normal_key = 9'h16a;
            8'h3c: normal_key = 9'h175;
            8'h3d: normal_key = 9'h137;
            8'h3e: normal_key = 9'h138;
            8'h41: normal_key = 9'h12c;
            8'h42: normal_key = 9'h16b;
            8'h43: normal_key = 9'h169;
            8'h44: normal_key = 9'h16f;
            8'h45: normal_key = 9'h130;
            8'h46: normal_key = 9'h139;
            8'h49: normal_key = 9'h12e;
            8'h4a: normal_key = 9'h12f;
            8'h4b: normal_key = 9'h16c;
            8'h4c: normal_key = 9'h13b;
            8'h4d: normal_key = 9'h170;
            8'h4e: normal_key = 9'h12d;
            8'h52: normal_key = 9'h127;
            8'h54: normal_key = 9'h15b;
            8'h55: normal_key = 9'h13d;
            8'h5a: normal_key = 9'h10a;
            8'h5b: normal_key = 9'h15d;
            8'h5d: normal_key = 9'h15c;
            8'h61: normal_key = 9'h15c;
            8'h66: normal_key = 9'h118;
            8'h69: normal_key = 9'h131;
            8'h6b: normal_key = 9'h134;
            8'h6c: normal_key = 9'h137;
            8'h70: normal_key = 9'h130;
            8'h71: normal_key = 9'h12e;
            8'h72: normal_key = 9'h132;
            8'h73: normal_key = 9'h135;
            8'h74: normal_key = 9'h136;
            8'h75: normal_key = 9'h138;
            8'h78: normal_key = 9'h10f;
            8'h79: normal_key = 9'h12b;
            8'h7a: normal_key = 9'h133;
            8'h7b: normal_key = 9'h12d;
            8'h7c: normal_key = 9'h12a;
            8'h7d: normal_key = 9'h139;
            8'h83: normal_key = 9'h184;
            default: normal_key = 0;
        endcase end
    endfunction
    function [7:0] shift_symbol;
        input [7:0] ch;
        begin case(ch)
            8'h60: shift_symbol = 8'h7e;
            8'h31: shift_symbol = 8'h21;
            8'h32: shift_symbol = 8'h40;
            8'h33: shift_symbol = 8'h23;
            8'h34: shift_symbol = 8'h24;
            8'h35: shift_symbol = 8'h25;
            8'h36: shift_symbol = 8'h5e;
            8'h37: shift_symbol = 8'h26;
            8'h38: shift_symbol = 8'h2a;
            8'h39: shift_symbol = 8'h28;
            8'h30: shift_symbol = 8'h29;
            8'h2d: shift_symbol = 8'h5f;
            8'h3d: shift_symbol = 8'h2b;
            8'h5b: shift_symbol = 8'h7b;
            8'h5d: shift_symbol = 8'h7d;
            8'h5c: shift_symbol = 8'h7c;
            8'h3b: shift_symbol = 8'h3a;
            8'h27: shift_symbol = 8'h22;
            8'h2c: shift_symbol = 8'h3c;
            8'h2e: shift_symbol = 8'h3e;
            8'h2f: shift_symbol = 8'h3f;
            default: shift_symbol = ch;
        endcase end
    endfunction
    (* syn_ramstyle="block_ram" *) reg [8:0] normal_rom[0:255];
    (* syn_ramstyle="block_ram" *) reg [7:0] shift_rom[0:255];
    reg [8:0] decoded;
    reg [7:0] shifted_symbol;
    integer rom_index;
    initial for(rom_index=0;rom_index<256;rom_index=rom_index+1)begin
        normal_rom[rom_index]=normal_key(rom_index[7:0]);
        shift_rom[rom_index]=shift_symbol(rom_index[7:0]);
    end
    always @(posedge clk)begin
        decoded<=normal_rom[scan_code];
        shifted_symbol<=shift_rom[decoded[7:0]];
    end
    reg [8:0] mapped;
    reg [5:0] modifier_mask;
    always @* begin
        modifier_mask = 0;
        if(!extended && scan_code == 8'h12) modifier_mask = 1;
        else if(!extended && scan_code == 8'h59) modifier_mask = 2;
        else if(scan_code == 8'h14) modifier_mask = extended ? 8 : 4;
        else if(scan_code == 8'h11) modifier_mask = extended ? 32 : 16;
        mapped = 0;
        if(extended) begin
            case(scan_code)
                8'h6b: mapped = 9'o410;
                8'h74: mapped = 9'o431;
                8'h75: mapped = 9'o432;
                8'h72: mapped = 9'o433;
                8'h6c: mapped = 9'o423;
                8'h5a: mapped = 9'o412;
                8'h4a: mapped = 9'h12f;
                default: mapped = 0;
            endcase
        end else mapped = decoded;
        if(bios_mode && extended) begin
            case(scan_code)
                8'h6b: mapped = 9'h111;
                8'h74: mapped = 9'h112;
                8'h75: mapped = 9'h113;
                8'h72: mapped = 9'h114;
                default: begin end
            endcase
        end
        if(mapped[8]) begin
            if(mapped[7:0] >= "a" && mapped[7:0] <= "z") begin
                if((|modifiers[1:0]) ^ caps) mapped[7:0] = mapped[7:0] - 8'd32;
            end else if(|modifiers[1:0] && scan_code < 8'h69) mapped[7:0] = shifted_symbol;
            if(|modifiers[3:2]) begin
                case(scan_code)
                    8'h34: mapped = 9'o407;
                    8'h3a: mapped = 9'o415;
                    8'h4d: mapped = 9'o420;
                    8'h2d: mapped = 9'o422;
                    8'h2c: mapped = 9'o424;
                    8'h3c: mapped = 9'o425;
                    default: begin end
                endcase
            end
            if(|modifiers[5:4]) mapped[7] = 1;
        end
    end
    always @(posedge clk) begin
        if(reset) begin
            extended <= 0; released <= 0; pause_left <= 0;
            modifiers <= 0; caps <= 0; held <= 0; caps_down <= 0; last_key <= 0; decode_stage <= 0; stop <= 0; valid <= 0; code <= 0;
        end else begin
            if(valid && (ready || !bios_mode)) valid <= 0;
            if(!scan_valid || scan_pop)decode_stage<=0;
            else if(decode_stage!=2)decode_stage<=decode_stage+1'b1;
            if(scan_pop) begin
                if(scan_code == 0 || scan_code == 8'haa || scan_code == 8'hff) begin
                    modifiers <= 0; extended <= 0; released <= 0; pause_left <= 0;
                    held <= 0; caps_down <= 0; stop <= 0;
                end else if(pause_left != 0) pause_left <= pause_left - 1'b1;
                else if(scan_code == 8'he1) begin pause_left <= 7; extended <= 0; released <= 0; end
                else if(scan_code == 8'he0) extended <= 1;
                else if(scan_code == 8'hf0) released <= 1;
                else begin
                    extended <= 0; released <= 0;
                    if(released && key_index==last_key)held<=0;
                    if(modifier_mask != 0)
                        modifiers <= released ? modifiers & ~modifier_mask : modifiers | modifier_mask;
                    else if(!extended && scan_code == 8'h58) begin
                        caps_down<=!released;
                        if(!released && !caps_down) caps <= !caps;
                    end else if(!extended && scan_code == 8'h76) begin
                        if(bios_mode) begin
                            stop <= 0;
                            if(!released) begin code <= 8'h1b; valid <= 1; end
                        end else stop <= !released;
                    end
                    else if(!released && mapped[8]) begin
                        held<=1; last_key<=key_index;
                        // Native BK has one character latch, not a terminal FIFO.
                        // Drop extra characters while occupied; retain BIOS input
                        // ordering through its existing ready/valid handshake.
                        if(bios_mode || ready) begin code <= mapped[7:0]; valid <= 1; end
                    end
                end
            end
        end
    end
endmodule
