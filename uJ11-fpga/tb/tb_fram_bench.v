`timescale 1ns/1ps
module tb_fram_bench #(parameter integer MEMORY_MODE=0);
    reg clk=0,reset=1;
    wire cs_n,sck,mosi,miso,io_request,io_write,io_byte,stopped,retire,rf_write;
    wire memory_request,memory_write,memory_ack;
    wire [15:0] io_address,io_wdata,ir,mdr,psw,q,rf_data;
    wire [1:0] fault;
    wire [9:0] upc;
    wire [35:0] uword;
    wire [3:0] rf_address;
    integer w,k,n,clocks,transactions,start_spi,spi_edges=0,start_edges,file;
    reg [15:0] opcode;
    string names[0:8];
    uj11_fram_system #(.MEMORY_MODE(MEMORY_MODE)) dut(.irq_valid(1'b0),.irq_priority(3'b0),.irq_vector(8'b0),.irq_ack(),.waiting(),.peripheral_reset(),.clk(clk),.reset(reset),.spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),
        .spi_miso(miso),.io_request(io_request),.io_write(io_write),.io_byte(io_byte),
        .io_address(io_address),.io_wdata(io_wdata),.io_rdata(16'b0),.io_ack(1'b1),.io_error(1'b1),
        .stopped(stopped),.retire(retire),.fault_code(fault),.debug_upc(upc),.debug_uword(uword),
        .ir(ir),.mdr(mdr),.psw(psw),.q(q),.debug_rf_write(rf_write),
        .debug_rf_address(rf_address),.debug_rf_data(rf_data),
        .memory_request(memory_request),.memory_write(memory_write),.memory_ack(memory_ack));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
`ifdef VENDOR_ROM
    GSR GSR_INST(.GSR(1'b1));
    PUR PUR_INST(.PUR(1'b1));
`endif
    always #5 clk=~clk;
    always @(posedge sck) if(!cs_n) spi_edges=spi_edges+1;
    always @(posedge clk) if($test$plusargs("trace") && memory_request && memory_ack)
        $display("beat pc=%h data=%h cs=%b edges=%0d",dut.address,dut.rdata,cs_n,spi_edges);
    task tick; begin @(posedge clk); #1; end endtask
    initial begin
        names[0]="MOV_RR_loop"; names[1]="ADD_RR_loop"; names[2]="CMP_RR_loop";
        names[3]="mixed_RR_loop"; names[4]="BR_self";
        names[5]="BIT_RR_loop";names[6]="BIC_RR_loop";names[7]="BIS_RR_loop";names[8]="SUB_RR_loop";
        file=$fopen($sformatf("build/fram-benchmarks-%0d.json",MEMORY_MODE),"w");
        $fwrite(file,"[\n");
        #100;
        for(w=0;w<9;w=w+1) begin
            @(negedge clk); reset=1;
            tick; @(negedge clk); reset=0;
            repeat(17) tick;
            @(negedge clk);
            for(k=0;k<7;k=k+1) dut.core.engine.dp.rf.words[k]=k;
            for(k=0;k<64;k=k+1) begin
                case(w)
                    0: opcode=16'o010102;
                    1: opcode=16'o060102;
                    2: opcode=16'o020102;
                    3: case(k%4)
                        0: opcode=16'o010102;
                        1: opcode=16'o060302;
                        2: opcode=16'o020102;
                        3: opcode=16'o060401;
                    endcase
                    5: opcode=16'o030102;
                    6: opcode=16'o040102;
                    7: opcode=16'o050102;
                    8: opcode=16'o160102;
                    4: opcode=16'o000777;
                endcase
                if(k==63 && w!=4) opcode=16'o000700;
                fram.memory[k*2]=opcode[7:0]; fram.memory[k*2+1]=opcode[15:8];
            end
            n=0;
            while(n<64) begin tick; if(stopped) $fatal(1,"FRAM warmup stop %h",fault); if(retire) n=n+1; end
            n=0; clocks=0; transactions=0; start_spi=fram.transaction_count; start_edges=spi_edges;
            while(n<512) begin
                @(posedge clk);
                if(memory_request && memory_ack) transactions=transactions+1;
                #1; clocks=clocks+1;
                if(stopped || io_request || memory_write || clocks>100000) $fatal(1,"FRAM benchmark stop/access");
                if(retire) n=n+1;
            end
            if(transactions!=n) $fatal(1,"wrong CPU transactions");
            $display("FRAM %s: %0d clocks / %0d instructions, %0d CS transactions, %0d SCK rises",names[w],clocks,n,fram.transaction_count-start_spi,spi_edges-start_edges);
            $fwrite(file,"  {\"workload\":\"%s\",\"instructions\":%0d,\"microclocks\":%0d,\"memory_cycles\":%0d,\"spi_transactions\":%0d,\"spi_clocks\":%0d}%s\n",
                names[w],n,clocks,transactions,fram.transaction_count-start_spi,spi_edges-start_edges,w==8 ? "" : ",");
        end
        $fwrite(file,"]\n"); $fclose(file);
        $display("PASS FRAM benchmarks: actual SPI controller/model, 9 x 512 retirements");
        $finish;
    end
    initial begin #10000000; $fatal(1,"FRAM benchmark timeout"); end
endmodule
