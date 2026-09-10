`timescale 1ns/1ps
module tb_mmu_translate;
    parameter integer FULL=1;
    reg enabled=0,map22=0,writing=0,invalid_mode=0;
    reg [15:0] virtual_address=0,par=0,pdr=0;
    wire [21:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    integer checks=0,a,b,c,m,limit,offset;
    reg [7:0] reached[0:131071];
    reg [31:0] random_state=32'hf38cd120;
    wire [17:0] previous_pa;
    wire [2:0] previous_abort;
    wire previous_ram,previous_io,previous_nxm;
    uj11_mmu_translate dut(.*);
    // CP31 remains an independent miter for all 18-bit cases. Its PA is local
    // 18-bit; normalize only the I/O page to compare with canonical PA22.
    uj11_mmu_translate18 previous(.enabled(enabled),.writing(writing),
        .invalid_mode(invalid_mode),.virtual_address(virtual_address),.par(par[11:0]),
        .pdr(pdr[14:0]),.physical_address(previous_pa),.abort_flags(previous_abort),
        .ram_selected(previous_ram),.io_selected(previous_io),.nxm(previous_nxm));

    task check;
        integer expected_address,low_bound,high_bound,relative_address,modulus;
        reg [2:0] errors;
        reg readable,writable;
        begin
            #1;
            // Byte-address arithmetic independent of RTL's split block sum.
            modulus=map22 ? 4194304 : 262144;
            expected_address={16'b0,virtual_address};
            if(enabled)begin
                expected_address=(par*64+({16'b0,virtual_address}%8192))%modulus;
                if(!map22 && expected_address>=253952)expected_address=expected_address+3932160;
            end else if(virtual_address>=16'o160000)expected_address=expected_address+4128768;
            relative_address={16'b0,virtual_address}%8192;
            low_bound=pdr[3] ? (({16'b0,pdr}/256)%128)*64 : 0;
            high_bound=pdr[3] ? 8191 : (({16'b0,pdr}/256)%128)*64+63;
            case(({16'b0,pdr}/2)%4)
                0,2:begin readable=0;writable=0;end
                1:begin readable=1;writable=0;end
                default:begin readable=1;writable=1;end
            endcase
            errors=0;
            if(enabled)begin
                if(!readable || invalid_mode)errors[2]=1;
                if(relative_address<low_bound || relative_address>high_bound)errors[1]=1;
                if(writing && readable && !writable)errors[0]=1;
            end
            if(physical_address!==expected_address[21:0] || abort_flags!==errors ||
               ram_selected!==(expected_address<131072) ||
               io_selected!==(expected_address>=4186112) ||
               nxm!==((expected_address>=131072)&&(expected_address<4186112)))
                $fatal(1,"MMU va=%o par=%o pdr=%o en/22/wr/mode=%b%b%b%b pa=%o/%o abort=%b/%b classes=%b%b%b",
                    virtual_address,par,pdr,enabled,map22,writing,invalid_mode,
                    physical_address,expected_address,abort_flags,errors,ram_selected,io_selected,nxm);
            if({ram_selected,io_selected,nxm}!=3'b100 &&
               {ram_selected,io_selected,nxm}!=3'b010 &&
               {ram_selected,io_selected,nxm}!=3'b001)$fatal(1,"address classes overlap");
            if(!map22 && (physical_address!=={{4{previous_io}},previous_pa} ||
                abort_flags!==previous_abort || {ram_selected,io_selected,nxm}!==
                {previous_ram,previous_io,previous_nxm}))$fatal(1,"CP31 18-bit compatibility");
            checks=checks+1;
        end
    endtask
    initial begin
        // All virtual bytes, poisoned PAR/PDR/mode, MMR3 switches while off.
        invalid_mode=1;writing=1;pdr=0;par=16'hffff;
        for(a=0;a<65536;a=a+1)begin
            virtual_address=a[15:0];map22=0;check();map22=1;check();
        end
        enabled=1;invalid_mode=0;writing=0;pdr=16'o77406;
        // Every PAR16 and block offset: carry/overflow at both physical widths.
        // The shorter four-state run keeps every PAR and boundary block.
        for(a=0;a<65536;a=a+1)begin
            par=a[15:0];
            for(b=0;b<128;b=b+(FULL!=0 ? 1 : 127))begin
                virtual_address={a[2:0],b[6:0],6'b0};map22=0;check();map22=1;check();
                virtual_address=virtual_address+16'd63;map22=0;check();map22=1;check();
            end
        end
        // All PDR bits (including reserved/BC/W) at both inclusive limits and
        // the closest invalid byte, in both mappings/read-write/illegal-mode.
        for(a=0;a<65536;a=a+1)begin
            pdr=a[15:0];limit=(a/256)%128;
            for(b=0;b<4;b=b+1)begin
                case(b)
                    0:offset=limit*64;
                    1:offset=limit*64+63;
                    2:offset=(limit*64+8191)%8192;
                    default:offset=(limit*64+64)%8192;
                endcase
                virtual_address=16'h6000+offset[15:0];
                for(c=0;c<8;c=c+1)begin
                    map22=c[2];writing=c[0];invalid_mode=c[1];check();
                end
            end
        end
        // Every installed byte exactly once through the same virtual page,
        // independently in both enabled widths. Check all byte offsets here.
        writing=0;invalid_mode=0;pdr=16'o77406;
        for(m=0;m<2;m=m+1)begin
            map22=m[0];
            for(a=0;a<131072;a=a+1)reached[a]=0;
            for(a=0;a<16;a=a+1)begin
                par=a[15:0]*16'd128;
                for(b=0;b<8192;b=b+1)begin
                    virtual_address=16'h2000+b[15:0];check();
                    if(!ram_selected || abort_flags!=0)$fatal(1,"installed RAM unreachable");
                    reached[physical_address[16:0]]=reached[physical_address[16:0]]+1'b1;
                end
            end
            for(a=0;a<131072;a=a+1)if(reached[a]!=1)$fatal(1,"RAM alias/hole %o",a);
        end
        // Changes between requests must not leave stale PA/rights or width.
        for(a=0;a<100000;a=a+1)begin
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            virtual_address=random_state[15:0];par=random_state[31:16];
            pdr=random_state[23:8];
            {enabled,map22,writing,invalid_mode}=a[3:0];check();
        end
        $display("PASS MMU18/22: %0d checks FULL=%0d; PAR16/PA22, canonical I/O, PDR, CP31 miter, 128 KiB bijection, mode switches/NXM",checks,FULL);
        $finish;
    end
endmodule
