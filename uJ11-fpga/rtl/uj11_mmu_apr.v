`timescale 1ns/1ps
// Serialized APR storage access, DEC EK-DCJ11-UG-PRE 4.5.1/4.5.2.
// entry is selected by the caller; no CPU mode or physical CSR decode here.
// Keep all command inputs stable until ready, then drop request. Held request
// never repeats. byte_enable is lane-aligned, 00 is a no-op write.
// A nonempty CSR write clears paired PDR.W, taking priority over mark_written.
// mark_written only changes W; its architectural issue conditions belong to
// the CPU/MMR controller, which is not present in this isolated checkpoint.
module uj11_mmu_apr(input wire clk, reset, request,
    input wire [5:0] entry, input wire pdr_select, writing, mark_written,
    input wire [1:0] byte_enable, input wire [15:0] write_data,
    output wire [15:0] read_data, output reg ready, output wire busy);
    localparam READ=2'd0, UPDATE=2'd1, PAR_WRITE=2'd2, HOLD=2'd3;
    reg [1:0] phase;
    wire csr_write=writing && (|byte_enable);
    wire modify=csr_write || mark_written;
    wire par_write=csr_write && !pdr_select;
    wire memory_enable=!reset && ((phase==READ && request) ||
                                 (phase==UPDATE && modify) || phase==PAR_WRITE);
    // In disabled-memory phases address/WE are don't-care. Use state bits
    // directly; READ has WE=0, UPDATE selects PDR, PAR_WRITE selects PAR.
    wire [6:0] address={entry, !phase[1] && (modify || pdr_select)};
    wire [1:0] lanes={
        byte_enable[1] && (phase[1] || (phase[0] && writing && pdr_select)),
        (phase[0] && modify) || (phase[1] && byte_enable[0])};
    wire [2:0] controls=(csr_write && pdr_select && byte_enable[0]) ?
                        write_data[3:1] : read_data[3:1];
    wire [7:0] low_data=phase[0] ?
        {1'b0,mark_written && !csr_write,2'b0,controls,1'b0} : write_data[7:0];
    uj11_mmu_apr_ram ram(.clk(clk),.enable(memory_enable),.address(address),
        .write_enable(lanes),.write_data({write_data[15:8],low_data}),.read_data(read_data));
    assign busy=phase!=READ;
    always @(posedge clk)begin
        ready<=0;
        if(reset)phase<=READ;
        else case(phase)
            READ:if(request)phase<=UPDATE;
            UPDATE:if(par_write)phase<=PAR_WRITE;
                   else begin phase<=HOLD;ready<=1;end
            PAR_WRITE:begin phase<=HOLD;ready<=1;end
            HOLD:if(!request)phase<=READ;
        endcase
    end
endmodule
