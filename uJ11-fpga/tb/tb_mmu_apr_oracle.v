`timescale 1ns/1ps
module tb_mmu_apr_oracle;
    reg clk=0,reset=1,request=0,writing=0,mark_written=0;
    reg [21:0] physical_address=0,command_address,expected_pa;
    wire selected,pdr_select,ready,busy;
    wire [5:0] entry;
    reg [1:0] byte_enable=0,command_control,command_lanes;
    reg [15:0] write_data=0,expected_par,expected_pdr,command_data;
    reg [15:0] virtual_address=0,par=0,pdr=0;
    reg map22=0;
    wire [15:0] read_data;
    wire [21:0] pa;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    integer fd,fields,wide,expected_fault,checks=0,commands=0,good=0,bad=0;
    always #5 clk=~clk;
    uj11_mmu_apr_decode decode(.*);
    uj11_mmu_apr apr(.clk(clk),.reset(reset),.request(request && selected),
        .entry(entry),.pdr_select(pdr_select),.writing(writing),.mark_written(mark_written),
        .byte_enable(byte_enable),.write_data(write_data),.read_data(read_data),.ready(ready),.busy(busy));
    uj11_mmu_translate translate(.enabled(1'b1),.map22(map22),.writing(1'b0),
        .invalid_mode(1'b0),.virtual_address(virtual_address),.par(par),.pdr(pdr),
        .physical_address(pa),.abort_flags(abort_flags),.ram_selected(ram_selected),
        .io_selected(io_selected),.nxm(nxm));
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task beat(input [21:0] address,input [1:0] control,lanes,
              input [15:0] datum,expected);
        integer guard;
        begin
            @(negedge clk);physical_address=address;request=1;
            writing=control[1];mark_written=control[0];byte_enable=lanes;write_data=datum;
            #1;if(!selected)$fatal(1,"C oracle CSR not selected %o",address);
            guard=0;
            while(!ready)begin @(posedge clk);#1;guard=guard+1;
                if(guard>4)$fatal(1,"APR oracle timeout");
            end
            if(control==0 && read_data!==expected)
                $fatal(1,"APR oracle CSR%o got%h expected%h case%0d",address,read_data,expected,checks);
            @(negedge clk);request=0;
            @(posedge clk);#1;if(ready || busy)$fatal(1,"APR oracle rearm");
            commands=commands+1;
        end
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        fd=$fopen("build/mmu-apr-oracle.txt","r");if(!fd)$fatal(1,"APR oracle missing");
        while(!$feof(fd))begin
            fields=$fscanf(fd,"%h %h %h %h %h %h %h %d %d %h\n",command_address,
                command_control,command_lanes,command_data,expected_par,expected_pdr,
                virtual_address,wide,expected_fault,expected_pa);
            if(fields!=10)$fatal(1,"APR oracle malformed");
            map22=wide!=0;
            beat(command_address,command_control,command_lanes,command_data,
                 command_address[5] ? expected_par : expected_pdr);
            beat(command_address|22'o40,0,0,0,expected_par);par=read_data;
            beat(command_address&~22'o40,0,0,0,expected_pdr);pdr=read_data;
            #1;
            if((|abort_flags)!==(expected_fault!=0))$fatal(1,"APR translate acceptance case%0d",checks);
            if(expected_fault==0)begin
                if(pa!==expected_pa)$fatal(1,"APR translate PA case%0d",checks);
                good=good+1;
            end else bad=bad+1;
            checks=checks+1;
        end
        $fclose(fd);
        if(checks!=65536 || good==0 || bad==0)$fatal(1,"APR corpus incomplete");
        $display("PASS APR C oracle: %0d cases / %0d CSR commands, %0d translated / %0d faults; byte/W/readback + serial PAR/PDR lookup",checks,commands,good,bad);
        $finish;
    end
endmodule
