`timescale 1ns/1ps
module tb_translate;
    reg enabled,map22,writing,invalid_mode;
    reg [15:0] virtual_address,par,pdr;
    wire [21:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    uj11_mmu_translate dut(.*);
    integer checks=0,n,m,b,l,ed,acf,wr;
    reg [31:0] expected;
    reg [2:0] errors;
    task verify;
        begin
            #1;
            expected=enabled ? (par*64+{19'b0,virtual_address[12:0]}) : virtual_address;
            if(!enabled)begin
                if(virtual_address>=16'o160000)expected=32'h3f0000|virtual_address;
            end else if(!map22)begin
                expected=expected & 32'h3ffff;
                if(expected>=32'h3e000)expected=expected | 32'h3c0000;
            end else expected=expected & 32'h3fffff;
            errors=0;
            if(enabled)begin
                if(invalid_mode || (pdr & 2)==0)errors[2]=1;
                if(pdr[3] ? virtual_address[12:6]<pdr[14:8] : virtual_address[12:6]>pdr[14:8])errors[1]=1;
                if(writing && (pdr & 6)==2)errors[0]=1;
            end
            if(physical_address!==expected[21:0] || abort_flags!==errors ||
               io_selected!==(expected>=32'h3fe000) || ram_selected!==(expected<32'h200000) ||
               nxm!==((expected>=32'h200000)&&(expected<32'h3fe000)))
                $fatal(1,"translate mismatch VA=%o PAR=%o PDR=%o enabled=%b map22=%b PA=%o expected=%o flags=%b expected=%b",virtual_address,par,pdr,enabled,map22,physical_address,expected,abort_flags,errors);
            checks=checks+1;
        end
    endtask
    initial begin
        pdr=16'o177406;writing=0;invalid_mode=0;
        for(n=0;n<65536;n=n+1)begin
            virtual_address=n[15:0];par=n[15:0];
            enabled=0;map22=0;verify();map22=1;verify();
            enabled=1;map22=0;verify();map22=1;verify();
        end
        enabled=1;par=16'h7fc0;invalid_mode=0;map22=1;
        for(b=0;b<128;b=b+1)for(l=0;l<128;l=l+1)for(ed=0;ed<2;ed=ed+1)
            for(acf=0;acf<4;acf=acf+1)for(wr=0;wr<2;wr=wr+1)begin
                virtual_address=b*64+63;pdr=l*256+ed*8+acf*2;writing=wr;
                invalid_mode=0;verify();invalid_mode=1;verify();
            end
        $display("PASS MMU translation: %0d cases",checks);$finish;
    end
endmodule
