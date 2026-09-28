`timescale 1ns/1ps
// Same transaction stream through cached and uncached MMUs. Compare every
// visible response, restart record and external beat, including fault hits.
module tb_mmu_cache;
`ifdef UJ11_VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));PUR PUR_INST(.PUR(1'b1));
`endif
    reg clk=0;always #5 clk=~clk;
    reg reset=1,peripheral_reset=0,request=0,writing=0,byte_access=0;
    reg [15:0] virtual_address=0,write_data=0,instruction_pc=0;
    reg [1:0] mode=0;
    reg data_space=0,physical=0,console=0,instruction_start=0,delta_valid=0;
    reg [21:0] physical_address=0;
    reg [2:0] delta_register=0;
    reg [4:0] delta_amount=0;
    wire [1:0] ready,bus_request,bus_write,bus_byte,bus_error;
    wire [2:0] fault[0:1];
    wire [15:0] read_data[0:1],mmr0[0:1],mmr1[0:1],mmr2[0:1],mmr3[0:1],bus_data[0:1];
    wire [21:0] bus_address[0:1];
    integer beats[0:1];reg [39:0] last_beat[0:1];
    genvar k;
    generate for(k=0;k<2;k=k+1)begin: model
        assign bus_error[k]=bus_address[k]>=22'h200000 && bus_address[k]<22'h3fe000;
        uj11_mmu #(.CACHE_ENABLE(k==0)) dut(.clk(clk),.reset(reset),.peripheral_reset(peripheral_reset),
            .request(request),.writing(writing),.byte_access(byte_access),.virtual_address(virtual_address),
            .write_data(write_data),.mode(mode),.data_space(data_space),.physical(physical),.console(console),
            .physical_address(physical_address),.instruction_start(instruction_start),.instruction_pc(instruction_pc),
            .delta_valid(delta_valid),.delta_register(delta_register),.delta_amount(delta_amount),
            .ready(ready[k]),.fault(fault[k]),.read_data(read_data[k]),
            .bus_request(bus_request[k]),.bus_write(bus_write[k]),.bus_byte(bus_byte[k]),
            .bus_address(bus_address[k]),.bus_data(bus_data[k]),.bus_ready(bus_request[k]),
            .bus_error(bus_error[k]),.bus_read_data(bus_address[k][15:0]^16'h5a5a),
            .mmr0(mmr0[k]),.mmr1(mmr1[k]),.mmr2(mmr2[k]),.mmr3(mmr3[k]));
        always @(posedge clk)begin
            if(reset)begin beats[k]<=0;last_beat[k]<=0;end
            else if(bus_request[k])begin
                beats[k]<=beats[k]+1;
                last_beat[k]<={bus_address[k],bus_write[k],bus_byte[k],bus_data[k]};
            end
        end
    end endgenerate
    integer checks=0,hits=0,misses=0,transactions=0;
    task check(input bit ok,input string why);
        begin checks++;if(!ok)$fatal(1,"transaction %0d: %s",transactions,why);end
    endtask
    task access(input [21:0] address,input bit wr,input bit byt,input [15:0] value,
        input [1:0] md,input bit ds,input bit phys,input bit dbg);
        integer ticks,t0,t1,held_beats;
        begin
            @(negedge clk);request=1;physical_address=address;virtual_address=address[15:0];
            writing=wr;byte_access=byt;write_data=value;mode=md;data_space=ds;physical=phys;console=dbg;
            ticks=0;t0=0;t1=0;transactions++;
            while(ready!=3 && ticks<30)begin
                @(negedge clk);ticks++;
                if(ready[0] && t0==0)t0=ticks;
                if(ready[1] && t1==0)t1=ticks;
            end
            check(ready==3,"completion timeout");
            check(fault[0]===fault[1],"fault differs");
            if(fault[0]==0)check(read_data[0]===read_data[1],"response differs");
            check({mmr0[0],mmr1[0],mmr2[0],mmr3[0]}==={mmr0[1],mmr1[1],mmr2[1],mmr3[1]},"MMR differs");
            check(beats[0]==beats[1] && last_beat[0]===last_beat[1],"external beat differs");
            check(t0==t1 || t0+2==t1,"unexpected cache latency");
            if(t0<t1)hits++;else misses++;
            held_beats=beats[0];repeat(3)@(negedge clk);
            check(ready==3 && beats[0]==held_beats && beats[1]==held_beats,"held request repeated");
            request=0;repeat(2)@(negedge clk);
        end
    endtask
    task csr(input [21:0] address,input [15:0] value);
        access(address,1,0,value,0,0,1,1);
    endtask
    function [21:0] apr(input integer md,input integer ds,input integer page,input bit par);
        apr=(md==0 ? 22'o17772300 : md==1 ? 22'o17772200 : 22'o17777600)+ds*16+page*2+(par ? 32 : 0);
    endfunction
    integer md,ds,page,j,n,oldhits;
    reg [31:0] rng=32'h43a5d921;
    initial begin
        repeat(3)@(negedge clk);reset=0;
        for(md=0;md<4;md++)if(md!=2)for(ds=0;ds<2;ds++)for(page=0;page<8;page++)begin
            csr(apr(md,ds,page,1),16'h100+md*256+ds*128+page*32);
            csr(apr(md,ds,page,0),16'o77406);
        end
        csr(22'o17772516,16'o27);csr(22'o17777572,1);
        // Both I/D entries survive alternating accesses; each mode/page is tagged.
        for(md=0;md<4;md++)if(md!=2)for(page=0;page<8;page++)begin
            oldhits=hits;
            for(j=0;j<4;j++)for(ds=0;ds<2;ds++)access(page*8192+j*2,0,0,0,md,ds,0,0);
            check(hits>=oldhits+6,"I/D locality did not hit");
        end
        // Duplicate unified-page entries, W write-through, then byte updates.
        csr(22'o17772516,16'o20);
        access(42,0,0,0,0,0,0,0);access(42,0,0,0,0,1,0,0);
        access(42,1,0,16'habcd,0,0,0,0);access(44,1,0,16'h1234,0,1,0,0);
        access(apr(0,0,0,0),0,0,0,0,0,1,1);
        check(read_data[0][6],"cached write did not mark PDR.W");
        access(apr(0,0,0,1)+1,1,1,16'h0200,0,0,1,1);
        access(42,0,0,0,0,0,0,0);access(42,0,0,0,0,1,0,0);
        access(apr(0,0,0,0),1,1,16'o2,0,0,1,1);
        // Read-only PDR is warmed before two protected writes and ODT probes.
        access(0,0,0,0,0,0,0,0);
        access(0,1,0,0,0,0,0,0);access(0,1,0,0,0,0,0,0);
        check(fault[0]==3,"cached protection failure not raised");
        access(0,1,0,0,0,1,0,1);
        access(apr(0,0,0,0),0,0,0,0,0,1,1);
        check(read_data[0][6],"faulting cached write did not mark W");
        // Length faults must be checked at the new offset, even on a hit.
        csr(apr(0,0,0,0),6);csr(22'o17777572,1);
        access(0,0,0,0,0,0,0,0);access(64,0,0,0,0,0,0,0);
        check(fault[0]==3 && mmr0[0][14],"cached page-length failure");
        // Reproducible mixed CSR writes, split/mode changes, fault hits and NXM.
        for(n=0;n<800;n++)begin
            rng={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};
            case(n%8)
                0:access(apr(rng[1] ? 3 : 0,rng[2],rng[5:3],rng[6])+(rng[7]&rng[8]),
                    1,rng[8],rng[31:16],0,0,1,1);
                1:csr(22'o17777572,1);
                2:csr(22'o17772516,{11'b0,rng[4:0]});
                default:begin
                    access({6'b0,rng[15:1],1'b0},rng[20],0,rng[31:16],rng[17:16],rng[18],0,rng[19]);
                    access({6'b0,rng[15:1],1'b0},rng[20],0,rng[31:16],rng[17:16],rng[18],0,rng[19]);
                end
            endcase
        end
        @(negedge clk);peripheral_reset=1;@(negedge clk);peripheral_reset=0;
        check(!model[0].dut.cache_i_valid && !model[0].dut.cache_d_valid,"RESET invalidation");
        csr(22'o17777572,1);access(0,0,0,0,0,0,0,0);access(0,0,0,0,0,0,0,0);
        check(hits>500,"insufficient cache-hit coverage");
        $display("PASS MMU cache: %0d checks, %0d transactions, %0d hits, %0d misses",checks,transactions,hits,misses);
        $finish;
    end
    initial begin #1000000;$fatal(1,"global timeout");end
endmodule
