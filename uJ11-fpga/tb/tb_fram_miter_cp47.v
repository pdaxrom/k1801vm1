`timescale 1ns/1ps
module tb_fram_miter_cp47;
    parameter integer CLK_DIV=1, QUALIFIED_DATA=1;
    reg clk=0,rst=1,req=0,writing=0,byte_access=0,bank=0;
    reg [15:0] address=0,data=0;
    reg miso=0;
    wire [15:0] gold_data,gate_data;
    wire gold_ready,gold_error,gold_busy,gold_cs,gold_sck,gold_mosi;
    wire gate_ready,gate_error,gate_busy,gate_cs,gate_sck,gate_mosi;
    integer clocks=0,checks=0,beats=0,seed=32'h71937147;
    integer j,k,i,limit;
    always #5 clk=~clk;
    uj11_fram_gold #(.CLK_DIV(CLK_DIV)) gold(clk,rst,req,writing,byte_access,bank,
        address,data,gold_data,gold_ready,gold_error,gold_busy,gold_cs,gold_sck,gold_mosi,miso);
    uj11_board_fram #(.CLK_DIV(CLK_DIV)) gate(clk,rst,req,writing,byte_access,bank,
        address,data,gate_data,gate_ready,gate_error,gate_busy,gate_cs,gate_sck,gate_mosi,miso);
    // Include X/Z on serial input and transmitted payload; control is known.
    always @(negedge clk) begin
        case(clocks%13)
            0:miso=1'bx;
            1:miso=1'bz;
            default:miso=clocks[2]^clocks[4];
        endcase
        clocks=clocks+1;
        #1;
        if({gold_ready,gold_error,gold_busy,gold_cs,gold_sck,gold_mosi}!==
           {gate_ready,gate_error,gate_busy,gate_cs,gate_sck,gate_mosi})
            $fatal(1,"SPI/handshake mismatch clock%0d",clocks);
        if((!QUALIFIED_DATA || gold_ready || !gold_busy) && gold_data!==gate_data)
            $fatal(1,"qualified rdata mismatch clock%0d gold%h gate%h",clocks,gold_data,gate_data);
        checks=checks+1;
    end
    task beat;
        begin
            @(posedge clk);#2;req=1;
            limit=0;
            while(!gold_ready)begin
                @(posedge clk);#2;limit=limit+1;
                if(limit>1500)$fatal(1,"miter timeout");
            end
            beats=beats+1;
            repeat(5)begin @(posedge clk);#2;if(gold_ready)$fatal(1,"repeat ACK");end
            req=0;repeat(3)@(posedge clk);#2;
        end
    endtask
    initial begin
        repeat(4)@(posedge clk);#2;rst=0;
        for(i=0;i<128;i=i+1)begin
            seed=seed^(seed<<13);seed=seed^(seed>>17);seed=seed^(seed<<5);
            writing=i[0];byte_access=i[1];bank=i[2];address=seed[15:0];
            case(i%4)
                0:data=16'hzzzz;
                1:data=16'hx5az;
                default:data=seed[31:16];
            endcase
            beat();
        end
        // Cancel at every clock offset through the longest write transaction.
        // With no memory model this also permits arbitrary MISO at reset.
        for(k=0;k<128*CLK_DIV;k=k+1)begin
            writing=1;byte_access=k[0];bank=k[1];address=16'hfffe;data=16'h963c;req=1;
            for(j=0;j<k;j=j+1)begin @(posedge clk);#2;end
            rst=1;@(posedge clk);#2;req=0;rst=0;
            repeat(2)@(posedge clk);#2;
        end
        $display("PASS CP47 FRAM miter: divider%0d %0d beats %0d reset offsets %0d clock comparisons, SPI/ACK and valid X/Z data",CLK_DIV,beats,128*CLK_DIV,checks);
        $finish;
    end
endmodule
