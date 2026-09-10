`timescale 1ns/1ps
module tb_mmu_apr;
    parameter integer FULL=1;
    reg clk=0,reset=1,request=0,pdr_select=0,writing=0,mark_written=0;
    reg [5:0] entry=0;
    reg [1:0] byte_enable=0;
    reg [15:0] write_data=0;
    wire [15:0] read_data;
    wire ready,busy;
    reg [15:0] pars[0:63],pdrs[0:63];
    integer checks=0,i,j,k,m,n;
    reg [31:0] random_state=32'h93f15ac2;
    always #5 clk=~clk;
    uj11_mmu_apr dut(.*);
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1)); PUR PUR_INST(.PUR(1'b1));
`endif
    task send(input [5:0] e,input bit p,wr,mark,input [1:0] lanes,input [15:0] value);
        integer cycles;
        reg [15:0] expected;
        reg changed;
        begin
            changed=wr && lanes!=0;
            if(changed)begin
                if(p)begin
                    if(lanes[0])pdrs[e][7:0]=value[7:0];
                    if(lanes[1])pdrs[e][15:8]=value[15:8];
                    pdrs[e]=pdrs[e]&16'hff0e;
                end else begin
                    if(lanes[0])pars[e][7:0]=value[7:0];
                    if(lanes[1])pars[e][15:8]=value[15:8];
                    pdrs[e]=pdrs[e]&16'hffbf;
                end
            end else if(mark)pdrs[e]=pdrs[e]|16'h0040;
            expected=p ? pdrs[e] : pars[e];
            @(negedge clk);request=1;entry=e;pdr_select=p;writing=wr;
            mark_written=mark;byte_enable=lanes;write_data=value;
            cycles=0;
            while(!ready)begin
                @(posedge clk);#1;cycles=cycles+1;
                if(cycles>4)$fatal(1,"APR timeout");
            end
            if(cycles!=(changed && !p ? 3 : 2))$fatal(1,"APR latency %0d",cycles);
            if(!changed && !mark && read_data!==expected)
                $fatal(1,"APR entry%0d PDR%b got%h expected%h",e,p,read_data,expected);
            if(!busy)$fatal(1,"APR ready without hold");
            // Payload may change after ACK, but request remains asserted.
            @(negedge clk);entry=~e;pdr_select=~p;writing=1;mark_written=1;
            byte_enable=3;write_data=~value;
            repeat(2)begin @(posedge clk);#1;
                if(ready || !busy)$fatal(1,"APR held request repeated");
            end
            @(negedge clk);request=0;
            @(posedge clk);#1;if(ready || busy)$fatal(1,"APR failed to rearm");
            checks=checks+1;
        end
    endtask
    task inspect(input [5:0] e);
        begin send(e,0,0,0,0,0);send(e,1,0,0,0,0);end
    endtask
    initial begin
        for(i=0;i<64;i=i+1)begin pars[i]=0;pdrs[i]=0;end
        repeat(3)@(negedge clk);reset=0;
        for(i=0;i<64;i=i+1)inspect(i[5:0]);
        // All 16-bit PAR/PDR word values. Distribute over every storage entry.
        for(i=0;i<65536;i=i+(FULL!=0 ? 1 : 257))begin
            send(i[5:0],1,1,0,3,i[15:0]);
            inspect(i[5:0]);
            send(i[5:0],0,0,1,0,0); // set W, preserving PAR and all PDR controls
            inspect(i[5:0]);
            send(i[5:0],0,1,1,3,~i[15:0]); // PAR write wins over mark W
            inspect(i[5:0]);
        end
        // Every entry, both register kinds, all four byte masks and all byte
        // patterns. Start each case with W=1; high-only writes must clear it.
        for(i=0;i<64;i=i+1)for(j=0;j<2;j=j+1)
            for(k=0;k<4;k=k+1)for(m=0;m<256;m=m+(FULL!=0 ? 1 : 85))begin
                send(i[5:0],1,0,1,0,0);
                send(i[5:0],j[0],1,0,k[1:0],{m[7:0],~m[7:0]});
                inspect(i[5:0]);
            end
        // No request: changing all inputs cannot write the store.
        repeat(1000)begin
            @(negedge clk);entry=entry+1'b1;write_data=write_data+16'd71;
            @(posedge clk);#1;if(ready || busy)$fatal(1,"APR idle activity");
        end
        for(i=0;i<10000;i=i+1)begin
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            send(random_state[5:0],random_state[6],random_state[7],random_state[8],
                 random_state[10:9],random_state[31:16]);
            inspect(random_state[5:0]);
        end
        // Reset before the first update edge suppresses every pending write.
        for(n=0;n<2;n=n+1)begin
            @(negedge clk);request=1;entry=0;pdr_select=n[0];writing=1;
            mark_written=1;byte_enable=3;write_data=16'hffff;
            @(posedge clk);#1;
            @(negedge clk);reset=1;
            repeat(3)begin @(posedge clk);#1;if(ready || busy)$fatal(1,"APR reset activity");end
            @(negedge clk);request=0;reset=0;
            inspect(0);
        end
        // Reset after PDR clear and before PAR write preserves committed PDR
        // effects, but does not execute the uncommitted PAR phase.
        send(0,1,0,1,0,0);
        @(negedge clk);request=1;entry=0;pdr_select=0;writing=1;
        mark_written=0;byte_enable=3;write_data=16'hffff;
        repeat(2)begin @(posedge clk);#1;end
        pdrs[0]=pdrs[0]&16'hffbf;
        @(negedge clk);reset=1;
        repeat(3)begin @(posedge clk);#1;if(ready || busy)$fatal(1,"APR partial reset");end
        @(negedge clk);request=0;reset=0;
        for(i=0;i<64;i=i+1)inspect(i[5:0]);
        $display("PASS APR: %0d commands FULL=%0d; PAR16/PDR, byte lanes, W set/clear, all 64 entries, held/idle/reset",checks,FULL);
        $finish;
    end
endmodule
