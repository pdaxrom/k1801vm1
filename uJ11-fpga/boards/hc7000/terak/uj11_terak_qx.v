`timescale 1ns/1ps
// Terak 8510 QX (500021, sections 6.2/6.3). The guest uses PIO, never DMA.
// SERV sees only a physical sector request and the 64-word transfer buffer.
module uj11_terak_qx #(parameter integer CLOCK_HZ=50000000,MECHANICAL_SPEEDUP=1)(
    input wire clk,reset,bus_reset,request,writing,
    input wire data_register,
    input wire [15:0] write_data,
    output wire [15:0] read_data,
    output wire ready,error,busy,
    output reg boot_qb,boot_menu,boot_rl,
    input wire irq_ack,
    output reg irq,
    input wire service_write,
    input wire [6:0] service_address,
    input wire [31:0] service_data,
    output wire [31:0] service_read
);
    localparam integer ID_WINDOW=(CLOCK_HZ/1000000)*300;
    reg [15:0] control,errors;
    reg done,seen,id_valid,backend;
    reg [31:0] delay_cycles,window_cycles;
    reg [2:0] function_code;
    reg [6:0] track[0:3];
    reg [4:0] rotation,sector;
    reg [5:0] index;
    reg [7:0] generation;
    reg [3:0] present,readonly;
    reg [15:0] buffer[0:63] /* synthesis syn_ramstyle = "distributed" */;
    wire buffer_service=service_write && service_address[6] && backend &&
        service_data[31:24]==generation;
    wire buffer_guest=request && !seen && !busy && writing && data_register && !id_valid;
    // One write port lets Synplify infer distributed RAM instead of 1024 FFs.
    always @(posedge clk)if(!reset && (buffer_service || buffer_guest))
        buffer[buffer_service ? service_address[5:0] : index]<=
            buffer_service ? service_data[15:0] : write_data;
    reg bus_error;
    reg [15:0] response;
    wire [1:0] unit=control[9:8];
    assign busy=!done;
    assign ready=request && seen;
    assign error=request && seen && bus_error;
    wire [15:0] value=data_register ? (id_valid ? {3'b0,sector,1'b0,track[unit]} : buffer[index]) :
        errors | {6'b0,track[unit]==0,1'b0,done,control[6:0]};
    assign read_data=response;
    assign service_read=service_address[6] ? {16'b0,buffer[service_address[5:0]]} :
        service_address==0 ? {31'b0,backend} :
        service_address==1 ? {generation,7'b0,function_code,unit,track[unit],sector} :
        service_address==3 ? {21'b0,boot_rl,boot_menu,boot_qb,readonly,present} : 0;
    integer i;
    always @(posedge clk)begin
        if(reset)begin
            control<=0;errors<=0;done<=1;seen<=0;irq<=0;index<=0;bus_error<=0;response<=0;
            delay_cycles<=0;window_cycles<=0;id_valid<=0;backend<=0;
            sector<=1;rotation<=1;function_code<=0;generation<=0;present<=0;readonly<=0;boot_qb<=0;boot_menu<=0;boot_rl<=0;
            for(i=0;i<4;i=i+1)track[i]<=0;
        end else begin
            if(!request)seen<=0;
            if(irq_ack)irq<=0;
            if(window_cycles!=0)window_cycles<=window_cycles-1'b1;
            else id_valid<=0;
            if(delay_cycles!=0)delay_cycles<=delay_cycles-1'b1;
            if(busy && !backend && delay_cycles==1)begin
                done<=1;
                if(control[6])irq<=1;
                if(function_code==4)begin
                    id_valid<=1;window_cycles<=ID_WINDOW;sector<=rotation;
                    rotation<=rotation==26 ? 5'd1 : rotation+1'b1;
                end
            end
            if(service_write)begin
                if(service_address==3)begin
                    present<=service_data[3:0];readonly<=service_data[7:4];
                    boot_qb<=service_data[8];boot_menu<=service_data[9];boot_rl<=service_data[10];
                end
                else if(service_address==2 && backend && generation==service_data[31:24])begin
                    errors<=service_data[15:0]&16'hfc00;backend<=0;done<=1;
                    if(control[6])irq<=1;
                end
            end
            if(request && !seen)begin
                seen<=1;response<=value;bus_error<=busy && (writing || data_register);
                if(!(busy && (writing || data_register)))begin
                    if(data_register && !id_valid)begin
                        index<=index+1'b1;
                    end else if(!data_register)begin
                        if(!id_valid)index<=0;
                        if(writing)begin
                            if(!write_data[6])irq<=0;
                            else if(!control[6] && done)irq<=1;
                            // Re-establish all control bits; every CSR write clears errors.
                            control<=write_data&16'h037e;errors<=0;
                            if(write_data[0])begin
                                control<=write_data&16'h037e;errors<=0;irq<=0;
                                function_code<=write_data[3:1];done<=0;id_valid<=0;window_cycles<=0;
                                delay_cycles<=CLOCK_HZ/1000000;
                                case(write_data[3:1])
                                    1:delay_cycles<=CLOCK_HZ/500/MECHANICAL_SPEEDUP; // 2 ms, including absent media
                                    2,3:begin
                                        delay_cycles<=CLOCK_HZ/1000*6/MECHANICAL_SPEEDUP;
                                        if(write_data[3:1]==2 && track[write_data[9:8]]<76)
                                            track[write_data[9:8]]<=track[write_data[9:8]]+1'b1;
                                        if(write_data[3:1]==3 && track[write_data[9:8]]!=0)
                                            track[write_data[9:8]]<=track[write_data[9:8]]-1'b1;
                                    end
                                    4:delay_cycles<=CLOCK_HZ/1000000*64;
                                    5,6,7:begin
                                        if(!id_valid || window_cycles==0)errors<=16'ha000;
                                        else if(write_data[3:1]!=5 && readonly[write_data[9:8]])errors<=16'h8800;
                                        else begin backend<=1;generation<=generation+1'b1;end
                                    end
                                    default:begin end
                                endcase
                                if(write_data[3:1]!=1 && !present[write_data[9:8]])begin
                                    errors<=16'h8000;backend<=0;delay_cycles<=CLOCK_HZ/1000000;
                                end
                            end
                        end
                    end
                end
            end
            // RESET invalidates an in-flight backend completion, but keeps mounts.
            if(bus_reset)begin
                control<=0;errors<=0;done<=1;seen<=0;irq<=0;index<=0;
                id_valid<=0;window_cycles<=0;delay_cycles<=0;backend<=0;generation<=generation+1'b1;
            end
        end
    end
endmodule
