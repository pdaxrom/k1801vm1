`timescale 1ns/1ps
module tb_mmu_apr_decode;
    reg [21:0] physical_address=0;
    wire selected,pdr_select;
    wire [5:0] entry;
    integer a,base,index,mode,space,page,checks=0,hits=0;
    reg expected;
    uj11_mmu_apr_decode dut(.*);
    initial begin
        // Full 4 MiB address space, not just the upper I/O page.
        for(a=0;a<4194304;a=a+1)begin
            physical_address=a[21:0];#1;
            expected=0;base=0;mode=0;
            if(a>=22'o17772200 && a<=22'o17772277)begin expected=1;base=22'o17772200;mode=1;end
            if(a>=22'o17772300 && a<=22'o17772377)begin expected=1;base=22'o17772300;mode=0;end
            if(a>=22'o17777600 && a<=22'o17777677)begin expected=1;base=22'o17777600;mode=3;end
            if(selected!==expected)$fatal(1,"APR CSR hit/alias pa%o selected%b",a,selected);
            if(expected)begin
                index=(a-base)/2;space=(index/8)%2;page=index%8;
                if({26'b0,entry}!==(mode*16+space*8+page) || pdr_select!==(index<16))
                    $fatal(1,"APR CSR fields pa%o entry%o pdr%b",a,entry,pdr_select);
                hits=hits+1;
            end
            checks=checks+1;
        end
        if(hits!=192)$fatal(1,"APR CSR coverage %0d",hits);
        $display("PASS APR CSR decode: %0d PA22 bytes, %0d selected; 96 registers, no mode2 or lower aliases",checks,hits);
        $finish;
    end
endmodule
