`timescale 1ns/1ps
module tb_mmu18;
    reg enabled=0,writing=0,invalid_mode=0;
    reg [15:0] virtual_address=0;
    reg [11:0] par=0;
    reg [14:0] pdr=0;
    wire [17:0] physical_address;
    wire [2:0] abort_flags;
    wire ram_selected,io_selected,nxm;
    integer checks=0,a,b,c,d,limit,offset;
    reg [7:0] reached[0:131071];
    uj11_mmu_translate18 dut(.*);

    // Independent byte-address arithmetic and DEC access-rights truth table.
    // Test selectors as address classes; caller must suppress ALL bus requests
    // on an MMU abort. NXM produces bus vector004, not MMU vector250.
    task check;
        integer expected_address,low_bound,high_bound,relative_address;
        reg [2:0] errors;
        reg readable,writable;
        begin
            #1;
            expected_address=virtual_address;
            if(enabled)expected_address=(par*64+(virtual_address%8192))%262144;
            else if(virtual_address>=16'o160000)expected_address=virtual_address+196608;
            relative_address=virtual_address%8192;
            low_bound=pdr[3] ? ((pdr/256)%128)*64 : 0;
            high_bound=pdr[3] ? 8191 : ((pdr/256)%128)*64+63;
            case((pdr/2)%4)
                0,2:begin readable=0;writable=0;end
                1:begin readable=1;writable=0;end
                3:begin readable=1;writable=1;end
            endcase
            errors=0;
            if(enabled)begin
                if(!readable || invalid_mode)errors[2]=1;
                if(relative_address<low_bound || relative_address>high_bound)errors[1]=1;
                if(writing && readable && !writable)errors[0]=1;
            end
            if(physical_address!==expected_address[17:0] || abort_flags!==errors ||
               ram_selected!==(expected_address<131072) ||
               io_selected!==(expected_address>=253952) ||
               nxm!==((expected_address>=131072)&&(expected_address<253952)))
                $fatal(1,"MMU va=%o par=%o pdr=%o en/wr/mode=%b%b%b pa=%o/%o abort=%b/%b ram/io/nxm=%b%b%b",
                    virtual_address,par,pdr,enabled,writing,invalid_mode,
                    physical_address,expected_address,abort_flags,errors,ram_selected,io_selected,nxm);
            if((ram_selected+io_selected+nxm)!=1)$fatal(1,"physical classes overlap");
            checks=checks+1;
        end
    endtask

    initial begin
        // Disabled mapping ignores ALL PDR errors; top 8 KiB is physical I/O.
        invalid_mode=1;writing=1;pdr=0;par=12'hfff;
        for(a=0;a<65536;a=a+1)begin virtual_address=a[15:0];check();end
        enabled=1;invalid_mode=0;writing=0;pdr=15'o77406;
        // All PAR values and all block offsets, both ends of each block.
        // Includes 18-bit overflow, the 128 KiB boundary and the real I/O page.
        for(a=0;a<4096;a=a+1)begin
            par=a[11:0];
            for(b=0;b<128;b=b+1)begin
                virtual_address={3'b101,b[6:0],6'b0};check();
                virtual_address=virtual_address+63;check();
            end
        end
        // Every writable/reserved PDR bit pattern; expansion and rights at
        // the exact lower/upper bounds and their nearest invalid block.
        for(a=0;a<32768;a=a+1)begin
            pdr=a[14:0];limit=(a/256)%128;
            for(b=0;b<4;b=b+1)begin
                case(b)
                    0:offset=limit*64;
                    1:offset=limit*64+63;
                    2:offset=(limit*64+8191)%8192;
                    3:offset=(limit*64+64)%8192;
                endcase
                virtual_address=16'h6000+offset[15:0];
                for(c=0;c<4;c=c+1)begin
                    writing=c[0];invalid_mode=c[1];check();
                end
            end
        end
        // Map each of the 16 physical 8 KiB pages through the SAME virtual
        // window. Every installed byte must be reachable exactly once.
        writing=0;invalid_mode=0;pdr=15'o77406;
        for(a=0;a<131072;a=a+1)reached[a]=0;
        for(a=0;a<16;a=a+1)begin
            par=a[11:0]*128;
            for(b=0;b<8192;b=b+1)begin
                virtual_address=16'h2000+b[15:0];check();
                if(!ram_selected || abort_flags!=0)$fatal(1,"installed RAM unreachable");
                reached[physical_address]=reached[physical_address]+1;
            end
        end
        for(a=0;a<131072;a=a+1)if(reached[a]!=1)$fatal(1,"physical RAM alias/hole %o",a);
        $display("PASS MMU18: %0d checks; all PAR/block offsets; all PDR patterns; disabled I/O; 128 KiB bijection; NXM no alias",checks);
        $finish;
    end
endmodule
