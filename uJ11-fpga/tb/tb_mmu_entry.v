`timescale 1ns/1ps
module tb_mmu_entry;
    reg clk=0,reset=1,enabled=0,running=1,memory_word=0,return_word=0;
    reg hold_routine=0,guest_advance=0;
    reg [9:0] upc=0;
    wire redirect,active,block_memory,stall;
    wire [9:0] address;
    reg pending=0,resumed=0;
    reg [9:0] original_pc;
    reg [31:0] random_state=32'hab18493c;
    integer checks=0,entries=0,returns=0,i,j;
    reg expected_enter,expected_return;
    uj11_mmu_entry dut(.clk(clk),.reset(reset),.enabled(enabled),.running(running),
        .memory_word(memory_word),.return_word(return_word),.hold_routine(hold_routine),
        .guest_advance(guest_advance),.upc(upc),.redirect(redirect),
        .redirect_address(address),.active(active),.block_memory(block_memory),.stall(stall));
    always #5 clk=~clk;
    task cycle;
        begin
            #1;
            expected_enter=running && enabled && memory_word && !pending;
            expected_return=running && pending && !resumed && return_word && !hold_routine;
            if(!reset)begin
                if({redirect,active,block_memory,stall} !==
                   {expected_enter || expected_return,pending && !resumed,
                    expected_enter || (pending && !resumed),pending && !resumed && hold_routine})
                    $fatal(1,"entry controls mismatch at check %0d",checks);
                if(redirect && address!==(expected_enter ? 10'h1cd : original_pc))
                    $fatal(1,"entry return address lost at check %0d",checks);
            end
            @(posedge clk);#1;
            if(reset)begin pending=0;resumed=0;end
            else if(expected_enter)begin pending=1;resumed=0;original_pc=upc;entries=entries+1;end
            else if(expected_return)begin resumed=1;returns=returns+1;end
            else if(running && guest_advance && resumed)begin pending=0;resumed=0;end
            checks=checks+1;
            @(negedge clk);
        end
    endtask
    initial begin
        cycle();reset=0;
        // Every return PC, spurious requests/commits in the routine, hold at
        // the return, enable changes, and an ack-less resumed FETCH phase.
        for(i=0;i<1024;i=i+1)begin
            enabled=1;running=1;memory_word=1;return_word=0;hold_routine=0;
            guest_advance=0;upc=i[9:0];cycle();
            for(j=0;j<4;j=j+1)begin
                upc=~i[9:0];enabled=j[0];guest_advance=j[1];
                return_word=1;hold_routine=1;cycle();
            end
            hold_routine=0;cycle();return_word=0;guest_advance=0;
            repeat(3)cycle(); // includes the synchronous opcode-lookup wait
            guest_advance=1;cycle();guest_advance=0;
        end
        for(i=0;i<200000;i=i+1)begin
            random_state=random_state^(random_state<<13);
            random_state=random_state^(random_state>>17);
            random_state=random_state^(random_state<<5);
            {enabled,running,memory_word,return_word,hold_routine,guest_advance,upc}=random_state[15:0];
            reset=i%97==0;cycle();
        end
        reset=1;cycle();reset=0;enabled=0;running=1;memory_word=1;
        return_word=1;hold_routine=1;guest_advance=1;repeat(4)cycle();
        $display("PASS MMU entry: %0d cycles, all 1024 return PCs, %0d entries / %0d returns; hold, resume, reset, disabled",checks,entries,returns);
        $finish;
    end
endmodule
