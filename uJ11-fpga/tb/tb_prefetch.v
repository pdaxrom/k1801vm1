`timescale 1ns/1ps
module tb_prefetch;
    reg clk=0,reset=1,request=0,write=0,byte_access=0,stream=0,pc_write=0,stopped=0,prefetch_enable=1;
    reg [15:0] address=0,wdata=0,pc_data=0;
    wire [15:0] rdata,io_address,io_wdata;
    wire ack,error,io_request,io_write,io_byte,cs_n,sck,mosi,miso;
    reg io_ack=1,io_error=0;
    reg [15:0] io_rdata=16'h1234;
    reg [7:0] expected[0:65535];
    reg [31:0] rng=32'h61d450e9;
    reg [15:0] a,v,got;
    integer i,j,cycles,checks=0,edges=0,cs_edges=0,io_beats=0,t0,e0,n0;
    reg got_error;
    // Slow enough for datasheet CS/SCK minima; this is a protocol test.
    always #17 clk=~clk;
    uj11_prefetch dut(.*,.spi_cs_n(cs_n),.spi_sck(sck),.spi_mosi(mosi),.spi_miso(miso));
    spi_fram_model fram(.cs_n(cs_n),.sck(sck),.mosi(mosi),.miso(miso));
    always @(negedge cs_n) cs_edges=0;
    always @(posedge sck) if(!cs_n) begin edges=edges+1; cs_edges=cs_edges+1; end
    always @(posedge cs_n) if(!reset && cs_edges%8) $fatal(1,"closed partial SPI byte");
    always @(posedge clk) begin
        if(io_request && (!request || !(&address[15:13]))) $fatal(1,"speculative I/O");
        if(io_request && io_ack) io_beats=io_beats+1;
    end
    task tick; begin @(posedge clk); #1; end endtask
    task idle; input integer count; begin repeat(count) tick; end endtask
    task randomize;
        begin rng=rng^(rng<<13); rng=rng^(rng>>17); rng=rng^(rng<<5); end
    endtask
    task beat;
        input wr,bt,st; input [15:0] at,dat;
        begin
            @(negedge clk); request=1; write=wr; byte_access=bt; stream=st; address=at; wdata=dat;
            cycles=0;
            begin : waiting
                forever begin
                    @(posedge clk); cycles=cycles+1;
                    if(ack) begin got=rdata; got_error=error; #1; disable waiting; end
                    if(cycles>250) $fatal(1,"beat timeout at %h owner=%d",at,dut.owner);
                    #1;
                end
            end
            @(negedge clk); request=0;
            checks=checks+1;
        end
    endtask
    task rd;
        input bt,st; input [15:0] at;
        reg [15:0] want;
        begin
            want=bt ? {8'b0,expected[at]} : {expected[at+16'd1],expected[at]};
            beat(0,bt,st,at,0);
            if(got_error || got!==want) $fatal(1,"read %h got%h expected%h error%b",at,got,want,got_error);
        end
    endtask
    task wr;
        input bt; input [15:0] at,dat;
        begin
            beat(1,bt,0,at,dat);
            if(got_error) $fatal(1,"write error");
            expected[at]=dat[7:0]; if(!bt) expected[at+16'd1]=dat[15:8];
            if(fram.memory[at]!==expected[at] || (!bt && fram.memory[at+16'd1]!==expected[at+16'd1]))
                $fatal(1,"write not committed exactly at ACK");
        end
    endtask
    task redirect;
        input [15:0] at;
        begin @(negedge clk); pc_data=at; pc_write=1; tick; @(negedge clk); pc_write=0; end
    endtask
    initial begin
        #100;
        for(i=0;i<65536;i=i+1) begin expected[i]=8'(i^(i>>8)^8'hb6); fram.memory[i]=expected[i]; end
        fram.memory[65536]=8'hd7;
        tick; @(negedge clk); reset=0;
        // First word plus one speculative word, then SCK must park indefinitely.
        t0=fram.transaction_count; e0=edges;
        rd(0,1,16'h0200); idle(80); n0=edges; idle(80);
        if(edges!=n0 || n0-e0!=64 || fram.transaction_count-t0!=1 || cs_n || !dut.buffer_valid)
            $fatal(1,"one-word buffer did not park");
        rd(0,1,16'h0202);
        if(cycles!=1) $fatal(1,"buffer hit not immediate");
        // The next stream word can be an extension: same interface, no decode.
        rd(0,1,16'h0204); rd(0,1,16'h0206);
        if(fram.transaction_count-t0!=1) $fatal(1,"lost sequential READ");
        // A byte immediate consumes one whole aligned stream word internally.
        rd(0,1,16'h0300);idle(80);t0=fram.transaction_count;
        rd(1,1,16'h0302);
        if(cycles!=1 || fram.transaction_count!=t0)$fatal(1,"byte immediate missed prefetched word");
        rd(0,1,16'h0304);
        if(fram.transaction_count!=t0)$fatal(1,"byte immediate broke retained word stream");
        prefetch_enable=0;rd(0,0,16'h0320);t0=fram.transaction_count;e0=edges;
        rd(1,1,16'h0340);rd(0,1,16'h0342);
        if(fram.transaction_count!=t0+1 || edges-e0!=64)$fatal(1,"cold byte stream did not consume full word");
        prefetch_enable=1;
        // A non-stream operand invalidates even at the predicted address.
        rd(0,0,16'h0208); rd(0,1,16'h020a);
        // Word/odd-byte writes, little-endian lanes and self-modifying code.
        idle(80); wr(0,16'h020c,16'h3142); rd(0,1,16'h020c);
        idle(80); wr(1,16'h020f,16'h00a5); rd(0,0,16'h020e); rd(1,0,16'h020f);
        t0=fram.transaction_count;
        beat(0,0,1,16'h0201,0);
        if(!got_error || fram.transaction_count!=t0) $fatal(1,"odd read reached FRAM");
        beat(1,0,0,16'h0201,16'hffff);
        if(!got_error || fram.transaction_count!=t0) $fatal(1,"odd write reached FRAM");
        // Redirect with every possible partial-word amount of speculative work.
        for(j=0;j<38;j=j+1) begin
            rd(0,1,16'h0400); idle(j); redirect(16'h0800); rd(0,1,16'h0800);
        end
        // A redirect to the predicted PC retains a correctly tagged buffer.
        rd(0,1,16'h0500); idle(80); t0=fram.transaction_count;
        redirect(16'h0502); rd(0,1,16'h0502);
        if(cycles!=1 || fram.transaction_count!=t0) $fatal(1,"matching PC write invalidated hit");
        // Every PC bit must invalidate, including bit 0 and nibble boundaries
        // of the timing-constrained parallel tag comparator.
        for(j=0;j<16;j=j+1) begin
            rd(0,1,16'h0400); idle(80); redirect(16'h0402 ^ (16'b1 << j));
            if(dut.prediction_valid || dut.buffer_valid) $fatal(1,"PC tag bit %0d ignored",j);
        end
        // No prefetch into I/O, including the last RAM word.
        rd(0,1,16'hdffe); idle(100); e0=edges; idle(100);
        if(edges!=e0 || !cs_n || dut.prediction_valid) $fatal(1,"prefetch crossed I/O boundary");
        t0=fram.transaction_count; n0=io_beats;
        beat(0,0,1,16'o177562,0);
        if(got!==16'h1234 || got_error || io_beats!=n0+1) $fatal(1,"I/O response");
        idle(100);
        if(fram.transaction_count!=t0 || io_beats!=n0+1) $fatal(1,"speculative CSR side effect");
        // Immediate I/O completion may discard an in-flight speculative read.
        rd(0,1,16'h0600); idle(5); n0=io_beats;
        beat(0,1,0,16'o177500,0); idle(80);
        if(io_beats!=n0+1 || !cs_n || dut.buffer_valid) $fatal(1,"I/O preemption");
        io_error=1; beat(0,0,1,16'o160000,0);
        if(!got_error) $fatal(1,"lost I/O error");
        io_error=0;
        // Stopping and reset cancel predictions; reset preserves FRAM contents.
        rd(0,1,16'h0900); idle(5); @(negedge clk); stopped=1; idle(80);
        if(!cs_n || dut.buffer_valid || ack) $fatal(1,"STOP did not drain/close");
        @(negedge clk); stopped=0; rd(0,1,16'h0900); idle(5);
        @(negedge clk); reset=1; tick;
        if(!cs_n || sck || ack || dut.buffer_valid) $fatal(1,"reset state");
        @(negedge clk); reset=0; rd(0,1,16'h0900);
        // Pausing speculation must preserve retained READ and demand service.
        prefetch_enable=0;rd(0,0,16'h0a00);t0=fram.transaction_count;e0=edges;
        rd(0,1,16'h0a00);idle(100);
        if(edges-e0!=48 || dut.buffer_valid || cs_n)$fatal(1,"paused speculation");
        rd(0,1,16'h0a02);
        if(fram.transaction_count!=t0+1 || edges-e0!=64)$fatal(1,"pause lost sequential demand");
        prefetch_enable=1;idle(80);rd(0,1,16'h0a04);
        if(cycles!=1)$fatal(1,"resume prefetch");
        // Deterministic randomized stream/data interleaving with independent RAM oracle.
        a=16'h1000;
        for(j=0;j<400;j=j+1) begin
            randomize; idle(rng%85);
            if(rng[3:0]==0) begin randomize; a={4'h1,rng[10:0],1'b0}; end
            if(rng[3:0]==1) begin v=rng[31:16]; wr(rng[4],a|{15'b0,rng[4]},v); end
            else rd(0,rng[2:0]!=0,a);
            a=a+2;
        end
        if(fram.memory[65536]!==8'hd7) $fatal(1,"accessed private FRAM bank");
        $display("PASS prefetch: %0d beats, tags/park/redirects/SMC/bytes/odd/I-O/reset/random",checks);
        $finish;
    end
    initial begin #10000000; $fatal(1,"prefetch timeout"); end
endmodule
