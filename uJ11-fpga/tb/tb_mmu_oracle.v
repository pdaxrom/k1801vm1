`timescale 1ns/1ps
module tb_mmu_oracle;
    reg [3:0] control=0;
    reg [15:0] virtual_address=0,par=0,pdr=0;
    wire [21:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    integer fd,fields,checks=0,good=0,bad=0,expected_fault;
    reg [21:0] expected_address;
    uj11_mmu_translate dut(.enabled(control[3]),.map22(control[2]),
        .writing(control[1]),.invalid_mode(control[0]),.virtual_address(virtual_address),
        .par(par),.pdr(pdr),.physical_address(physical_address),.abort_flags(abort_flags),
        .ram_selected(ram_selected),.io_selected(io_selected),.nxm(nxm));
    initial begin
        fd=$fopen("build/mmu-oracle.txt","r");if(!fd)$fatal(1,"MMU oracle missing");
        while(!$feof(fd))begin
            fields=$fscanf(fd,"%h %h %h %h %d %h\n",control,virtual_address,par,pdr,expected_fault,expected_address);
            if(fields!=6)$fatal(1,"MMU oracle malformed, fields%0d",fields);
            #1;
            if((|abort_flags)!==(expected_fault!=0))$fatal(1,"MMU C acceptance case%0d",checks);
            if(expected_fault==0)begin
                if(physical_address!==expected_address)$fatal(1,"MMU C PA case%0d got%o expected%o",checks,physical_address,expected_address);
                good=good+1;
            end else bad=bad+1;
            checks=checks+1;
        end
        $fclose(fd);
        if(checks!=262144 || good==0 || bad==0)$fatal(1,"incomplete corpus");
        $display("PASS MMU C oracle: %0d cases, %0d translated / %0d faults; existing core, local I/O canonicalized",checks,good,bad);
        $finish;
    end
endmodule
