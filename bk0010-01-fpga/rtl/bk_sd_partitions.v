`timescale 1ns/1ps
// BIOS descriptors and computed ends occupy 32 words of block RAM.
// A 16-bit ALU validates all 32-bit bounds/overlaps before enabling raw disks.
module bk_sd_partitions (
    input wire clk,power_on,enabled,config_write,
    input wire [1:0] config_address,
    input wire [15:0] config_data,
    output reg [15:0] config_read,
    input wire [1:0] drive,
    input wire [10:0] offset,
    input wire writing,
    output wire allowed,
    output wire [31:0] lba,
    output wire [3:0] present,readonly,
    output wire block_mode,configured
);
    localparam IDLE=0,ADD_START=1,ADD_LO_ADDR=2,ADD_LO=3,ADD_HI_ADDR=4,ADD_HI=5,ADD_DONE=6,
        BOOT_START=7,BOOT_BOUND=8,DRIVE_START=9,DRIVE_BOUND=10,FAT_FIRST=11,FAT_SECOND=12,
        OTHER_START=13,OTHER_FIRST=14,OTHER_SECOND=15,OTHER_NEXT=16,DRIVE_NEXT=17,
        CMP_HI_ADDR=18,CMP_HI=19,CMP_LO_ADDR=20,CMP_LO=21;
    (* syn_ramstyle="block_ram" *) reg [15:0] words[0:31];
    reg [15:0] left_q,right_q;
    reg [31:0] bases[0:3];
    reg [4:0] state=IDLE,cursor=0,clear_index=0,left_address=0,right_address=0;
    reg [4:0] compare_left=0,compare_right=0,compare_return=IDLE;
    reg [4:0] store_address=0;
    reg [15:0] store_data=0,control=0;
    reg clearing=1,store=0,carry=0;
    reg compare_high_less=0,compare_high_equal=0,compare_result=0;
    reg [2:0] bank=0;
    reg [4:0] base_nonzero=0,length_nonzero=0,short_extent=0,overflow=0;
    reg [3:0] valid=0;
    reg bad_boot=0,invalid_drive=0;
    reg [1:0] checked_drive=0,other_drive=0;
    wire checking=state!=IDLE;
    wire [4:0] bank_base={bank,2'b0};
    wire [4:0] bank_end=bank==4?5'd30:5'd22+{bank[1:0],1'b0};
    wire [4:0] current_end=5'd22+{checked_drive,1'b0};
    wire [4:0] other_end=5'd22+{other_drive,1'b0};
    wire [16:0] sum={1'b0,left_q}+{1'b0,right_q}+{16'b0,state==ADD_HI&&carry};
    wire config_store=enabled&&config_write&&config_address==2&&cursor<22;
    always @(posedge clk)begin
        left_q<=words[left_address];
        right_q<=words[checking?right_address:cursor];
        if(clearing)words[clear_index]<=0;
        else if(config_store)words[cursor]<=config_data;
        else if(store)words[store_address]<=store_data;
    end
    wire [32:0] address={1'b0,bases[drive]}+{22'b0,offset};
    assign present=valid;
    assign readonly=control[11:8];
    assign block_mode=control[0];
    assign configured=control[15]&&!checking&&!clearing;
    assign allowed=configured&&valid[drive]&&offset<1600&&!address[32]&&(!writing||!readonly[drive]);
    assign lba=address[31:0];
    always @*begin
        case(config_address)
            0:config_read={configured,control[14:0]};
            1:config_read={11'b0,cursor};
            2:config_read=cursor<22?right_q:16'b0;
            default:config_read=0;
        endcase
    end
    task compare32;
        input [4:0] left_low,right_low,next;
        begin
            compare_left<=left_low;compare_right<=right_low;compare_return<=next;
            left_address<=left_low+1'b1;right_address<=right_low+1'b1;state<=CMP_HI_ADDR;
        end
    endtask
    integer index;
    always @(posedge clk)begin
        store<=0;
        if(power_on)begin
            clearing<=1;clear_index<=0;state<=IDLE;cursor<=0;control<=0;valid<=0;
            left_address<=0;right_address<=0;compare_left<=0;compare_right<=0;compare_return<=IDLE;
            store<=0;store_address<=0;store_data<=0;carry<=0;bank<=0;
            compare_high_less<=0;compare_high_equal<=0;compare_result<=0;
            base_nonzero<=0;length_nonzero<=0;short_extent<=0;overflow<=0;
            bad_boot<=0;invalid_drive<=0;checked_drive<=0;other_drive<=0;
            for(index=0;index<4;index=index+1)bases[index]<=0;
        end else if(clearing)begin
            clear_index<=clear_index+1'b1;if(clear_index==31)clearing<=0;
        end else if(enabled&&config_write)begin
            valid<=0;state<=IDLE;
            case(config_address)
                0:begin
                    control<=config_data&16'h8f01;
                    if(config_data[15])begin bank<=0;state<=ADD_START;end
                end
                1:cursor<=config_data[4:0];
                2:begin control[15]<=0;if(cursor<31)cursor<=cursor+1'b1;end
                default:begin end
            endcase
        end else case(state)
            IDLE:begin end
            ADD_START:begin
                left_address<=bank_base;right_address<=bank_base+2;
                carry<=0;state<=ADD_LO_ADDR;
            end
            ADD_LO_ADDR:state<=ADD_LO;
            ADD_LO:begin
                store<=1;store_address<=bank_end;store_data<=sum[15:0];carry<=sum[16];
                base_nonzero[bank]<=left_q!=0;length_nonzero[bank]<=right_q!=0;
                short_extent[bank]<=right_q<1600;
                if(bank<4)bases[bank[1:0]][15:0]<=left_q;
                left_address<=left_address+1'b1;right_address<=right_address+1'b1;state<=ADD_HI_ADDR;
            end
            ADD_HI_ADDR:state<=ADD_HI;
            ADD_HI:begin
                store<=1;store_address<=bank_end+1'b1;store_data<=sum[15:0];overflow[bank]<=sum[16];
                base_nonzero[bank]<=base_nonzero[bank]||left_q!=0;
                length_nonzero[bank]<=length_nonzero[bank]||right_q!=0;
                short_extent[bank]<=short_extent[bank]&&right_q==0;
                if(bank<4)bases[bank[1:0]][31:16]<=left_q;
                state<=ADD_DONE;
            end
            ADD_DONE:begin
                if(bank==4)state<=BOOT_START;
                else begin bank<=bank+1'b1;state<=ADD_START;end
            end
            BOOT_START:begin
                bad_boot<=!base_nonzero[4]||!length_nonzero[4]||overflow[4];
                checked_drive<=0;other_drive<=0;compare32(20,30,BOOT_BOUND);
            end
            BOOT_BOUND:begin bad_boot<=bad_boot||compare_result;state<=DRIVE_START;end
            DRIVE_START:begin
                invalid_drive<=bad_boot||!base_nonzero[checked_drive]||short_extent[checked_drive]||overflow[checked_drive];
                other_drive<=0;compare32(20,current_end,DRIVE_BOUND);
            end
            DRIVE_BOUND:begin
                invalid_drive<=invalid_drive||compare_result;
                compare32({checked_drive,2'b0},30,FAT_FIRST);
            end
            FAT_FIRST:begin
                if(compare_result)compare32(16,current_end,FAT_SECOND);
                else state<=OTHER_START;
            end
            FAT_SECOND:begin invalid_drive<=invalid_drive||compare_result;state<=OTHER_START;end
            OTHER_START:begin
                if(other_drive==checked_drive||!length_nonzero[other_drive])state<=OTHER_NEXT;
                else if(overflow[other_drive])compare32({other_drive,2'b0},current_end,OTHER_SECOND);
                else compare32({checked_drive,2'b0},other_end,OTHER_FIRST);
            end
            OTHER_FIRST:begin
                if(compare_result)compare32({other_drive,2'b0},current_end,OTHER_SECOND);
                else state<=OTHER_NEXT;
            end
            OTHER_SECOND:begin invalid_drive<=invalid_drive||compare_result;state<=OTHER_NEXT;end
            OTHER_NEXT:begin
                if(other_drive==3)state<=DRIVE_NEXT;
                else begin other_drive<=other_drive+1'b1;state<=OTHER_START;end
            end
            DRIVE_NEXT:begin
                valid[checked_drive]<=!invalid_drive;
                if(checked_drive==3)state<=IDLE;
                else begin checked_drive<=checked_drive+1'b1;state<=DRIVE_START;end
            end
            CMP_HI_ADDR:state<=CMP_HI;
            CMP_HI:begin
                compare_high_less<=left_q<right_q;compare_high_equal<=left_q==right_q;
                left_address<=compare_left;right_address<=compare_right;state<=CMP_LO_ADDR;
            end
            CMP_LO_ADDR:state<=CMP_LO;
            CMP_LO:begin
                compare_result<=compare_high_less||(compare_high_equal&&left_q<right_q);
                state<=compare_return;
            end
            default:state<=IDLE;
        endcase
    end
endmodule
