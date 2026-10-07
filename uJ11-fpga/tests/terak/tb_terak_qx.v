`timescale 1ns/1ps
module tb_terak_qx;
    reg clk=0,reset=1,bus_reset=0,request=0,writing=0,dr=0,ack=0,sw=0;
    always #10 clk=~clk;
    reg [15:0] wd=0;
    reg [6:0] sa=0;
    reg [31:0] sd=0;
    wire [15:0] rd;
    wire ready,error,busy,irq;
    wire [31:0] sr;
    uj11_terak_qx qx(.clk(clk),.reset(reset),.bus_reset(bus_reset),.request(request),.writing(writing),
        .data_register(dr),.write_data(wd),.read_data(rd),.ready(ready),.error(error),.busy(busy),
        .irq_ack(ack),.irq(irq),.service_write(sw),.service_address(sa),.service_data(sd),.service_read(sr));
    reg [15:0] result;
    reg [31:0] descriptor;
    task access(input bit datareg,input bit write,input [15:0] data);
        @(negedge clk);request=1;dr=datareg;writing=write;wd=data;
        do @(negedge clk);while(!ready);
        result=rd;request=0;
        @(negedge clk);
    endtask
    task service(input [6:0] address,input [31:0] data);
        @(negedge clk);sa=address;sd=data;sw=1;
        @(negedge clk);sw=0;
    endtask
    task read_id;
        access(0,1,16'o31);wait(!busy);access(1,0,0);
        if(result[7:0]!=0 || result[15:8]<1 || result[15:8]>26)$fatal(1,"ID %o",result);
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;service(3,1);
        access(0,1,16'o100);if(!irq)$fatal(1,"IE edge on DONE must request interrupt");
        ack=1;@(negedge clk);ack=0;access(0,1,0);if(irq)$fatal(1,"IE clear");
        for(integer i=0;i<64;i++)access(1,1,16'h1000+i);
        read_id();access(0,1,16'o35);sa=0;#1;
        if(sr!=1)$fatal(1,"polled WRITE did not reach backend");
        sa=1;#1;descriptor=sr;
        for(integer i=0;i<64;i++)begin sa=64+i;#1;if(sr!=16'h1000+i)$fatal(1,"ID reset buffer at %d",i);end
        service(2,descriptor&32'hff000000);wait(!busy);
        read_id();access(0,1,16'o133);sa=1;#1;descriptor=sr;
        for(integer i=0;i<64;i++)service(64+i,(descriptor&32'hff000000)|(16'h2000+i));
        service(2,descriptor&32'hff000000);wait(!busy);
        if(!irq)$fatal(1,"BR4 IRQ missing");
        ack=1;@(negedge clk);ack=0;
        access(0,0,0);
        for(integer i=0;i<64;i++)begin access(1,0,0);if(result!=16'h2000+i)$fatal(1,"PIO read %d",i);end
        access(0,0,0);access(1,0,0);if(result!=16'h2000)$fatal(1,"CSR did not rewind buffer");
        read_id();repeat(15010)@(negedge clk);access(0,1,16'o33);wait(!busy);access(0,0,0);
        if((result&16'ha000)!=16'ha000)$fatal(1,"late command not reported");
        access(0,1,16'o3); // RTC independent of absent drive
        @(negedge clk);request=1;writing=1;dr=1;@(negedge clk);
        if(!error)$fatal(1,"busy QXDB did not bus-fault");
        request=0;@(negedge clk);wait(!busy);
        read_id();access(0,1,16'o33);sa=1;#1;descriptor=sr;
        bus_reset=1;@(negedge clk);bus_reset=0;
        service(2,(descriptor&32'hff000000)|16'hfc00);access(0,0,0);
        if(result[15:10] || busy || irq)$fatal(1,"stale completion survived RESET");
        service(64,(descriptor&32'hff000000)|16'hffff);
        access(0,0,0);access(1,0,0);if(result!=16'h2000)$fatal(1,"stale buffer write survived RESET");
        read_id();access(0,1,16'o33);
        service(64,(descriptor&32'hff000000)|16'hffff);sa=64;#1;
        if(sr!=16'h2000)$fatal(1,"stale buffer write corrupted new operation");
        $display("PASS QX PIO, polled writes, ID window, IRQ, bus fault, reset generation");$finish;
    end
    initial begin #100000000;$fatal(1,"QX timeout");end
endmodule
