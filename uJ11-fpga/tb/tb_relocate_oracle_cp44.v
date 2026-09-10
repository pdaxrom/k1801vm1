`timescale 1ns/1ps
module tb_relocate_oracle_cp44;
    reg clk=0,reset=1,enabled=0,map22=0,request=0,acknowledge=0;
    reg [15:0] virtual_address=0,lookup_data=0,par=0;
    reg lookup_grant=0;
    wire lookup_request,translated_request,ram_region,io_region;
    wire [6:0] lookup_address;wire [21:0] physical_address;
    reg [1:0] control;reg [21:0] expected;
    integer fd,fields,checks=0,stalls=0,j;
    always #5 clk=~clk;
    // Same unregistered-output synchronous EBR timing as the shared APR RAM.
    always @(posedge clk)if(lookup_request && lookup_grant)lookup_data<=par;
    uj11_mmu_relocate dut(.*);
    task check_address;
        begin
            if(!translated_request || physical_address!==expected ||
               ram_region!==(expected<22'h020000) || io_region!==(expected>=22'h3fe000))
                $fatal(1,"relocation case%0d VA%o PAR%h ctl%b got%o expected%o RAM%b IO%b",checks,virtual_address,par,control,physical_address,expected,ram_region,io_region);
        end
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        fd=$fopen("build/cp44-relocate-oracle.txt","r");if(!fd)$fatal(1,"oracle missing");
        while(!$feof(fd))begin
            fields=$fscanf(fd,"%h %h %h %h\n",control,virtual_address,par,expected);
            if(fields!=4)$fatal(1,"oracle malformed");
            enabled=control[0];map22=control[1];request=1;acknowledge=0;
            lookup_data=16'hxxxx;lookup_grant=0;#1;
            if(enabled)begin
                for(j=0;j<(checks%4);j=j+1)begin
                    if(!lookup_request || translated_request)$fatal(1,"lookup stall leaked request");
                    @(negedge clk);stalls=stalls+1;
                end
                if(lookup_address!=={3'b0,virtual_address[15:13],1'b0})$fatal(1,"wrong APR page");
                lookup_grant=1;@(posedge clk);#1;
                if(translated_request)$fatal(1,"EBR output used before capture edge");
                @(negedge clk);lookup_grant=0;
                @(posedge clk);#1;check_address();@(negedge clk);
            end else begin
                if(lookup_request)$fatal(1,"unmapped lookup");check_address();
                @(posedge clk);#1;check_address();@(negedge clk);
            end
            // A CSR can change enable/map22 or overwrite APR after launch.
            // The in-flight physical request must retain its original address.
            enabled=~enabled;map22=~map22;lookup_data=~par;
            repeat(2)begin @(posedge clk);#1;check_address();@(negedge clk);end
            acknowledge=1;@(negedge clk);request=0;acknowledge=0;
            @(negedge clk);checks=checks+1;
        end
        $fclose(fd);
        // Reset cancels both a pending EBR lookup and an accepted lookup.
        for(j=0;j<2;j=j+1)begin
            request=1;enabled=1;lookup_grant=j!=0;
            @(negedge clk);reset=1;#1;
            if(translated_request || lookup_request)$fatal(1,"reset did not inhibit request");
            @(negedge clk);request=0;lookup_grant=0;reset=0;@(negedge clk);
        end
        if(checks!=262144)$fatal(1,"oracle incomplete");
        $display("PASS CP44 relocation C oracle: %0d addresses, %0d stalled lookup edges; all PAR16 in 16/18/22 modes, PA22/RAM/IO/NXM, held CSR changes, reset",checks,stalls);
        $finish;
    end
endmodule
