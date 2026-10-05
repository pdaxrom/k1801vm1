`timescale 1ns/1ps
module tb_models;
    reg [2:0] model;
    reg [15:0] bank_register, address;
    reg [6:0] a16_mode;
    reg writing;
    wire responds;
    wire [19:0] physical;
    bk_memory_map map(model,bank_register,a16_mode,address,writing,responds,physical);
    integer fd, count, vectors = 0, m, b, a, addr, w, valid, expected;
    initial begin
        fd = $fopen("build/map-vectors.txt", "r");
        if(!fd) $fatal(1, "map vectors unavailable");
        while(!$feof(fd)) begin
            count = $fscanf(fd,"%h %h %h %h %h %h %h\n",m,b,a,addr,w,valid,expected);
            if(count == 7) begin
                model = m; bank_register = b; a16_mode = a; address = addr; writing = w;
                #1;
                if(responds !== valid[0] || (responds && physical !== expected[19:0]))
                    $fatal(1,"map mismatch model=%0d bank=%06o A16=%03o address=%06o write=%0d physical=%h expected=%h valid=%0d/%0d",
                           model,bank_register,a16_mode,address,writing,physical,expected,responds,valid);
                vectors = vectors + 1;
            end else if(count != -1) $fatal(1,"invalid map vector");
        end
        $fclose(fd);
        $display("PASS C/RTL model map: %0d vectors",vectors);
        $finish;
    end
endmodule
