    // A continued READ must still return the exact currently addressed
    // memory bytes on every architectural acceptance, including RK traffic.
    always @(posedge clk)if(!reset && dut.request && dut.acknowledge && dut.bus.fram_selected)begin
        if(!dut.writing && dut.rdata!=={fram.memory[{1'b0,dut.address[15:1],1'b1}],fram.memory[{1'b0,dut.address[15:1],1'b0}]})
            $fatal(1,"FRAM read cursor mismatch addr%o got%h expected%h",dut.address,dut.rdata,
                {fram.memory[{1'b0,dut.address[15:1],1'b1}],fram.memory[{1'b0,dut.address[15:1],1'b0}]});
        if(dut.writing)begin
            if(!dut.byte_access && {fram.memory[{1'b0,dut.address[15:1],1'b1}],fram.memory[{1'b0,dut.address[15:1],1'b0}]}!==dut.data)
                $fatal(1,"FRAM word write mismatch");
            if(dut.byte_access && fram.memory[{1'b0,dut.address}]!==dut.data[7:0])$fatal(1,"FRAM byte write mismatch");
        end
    end
