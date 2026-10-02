`timescale 1ns/1ps
// J-11 memory-management transaction controller. Two PAR/PDR cache entries
// keep the last instruction-space and data-space mappings in flip-flops.
// Protection is checked on every hit; CSR accesses always use the APR EBR.
// Data and byte lanes are bus-aligned. Hold request/payload through ready,
// then drop request for one clock. Faults never issue an external bus beat.
module uj11_mmu #(parameter CACHE_ENABLE=1) (
    input wire clk, reset, peripheral_reset,
    input wire request, writing, byte_access,
    input wire [15:0] virtual_address, write_data,
    input wire [1:0] mode,
    input wire data_space,
    input wire physical, console,
    input wire [21:0] physical_address,
    input wire instruction_start,
    input wire [15:0] instruction_pc,
    input wire delta_valid,
    input wire [2:0] delta_register,
    input wire [4:0] delta_amount,
    output reg ready,
    output reg [2:0] fault, // 0 success, 1 odd word, 2 bus error, 3 MMU abort
    output reg [15:0] read_data,
    output wire bus_request, bus_write, bus_byte,
    output wire [21:0] bus_address,
    output wire [15:0] bus_data,
    input wire bus_ready, bus_error,
    input wire [15:0] bus_read_data,
    output reg [15:0] mmr0, mmr1, mmr2, mmr3
);
    localparam IDLE=0, PAR_READ=1, PDR_READ=2, CHECK=3, DECODE=4,
               BUS=5, CSR_READ=6, CSR_UPDATE=7, CSR_PAR=8, HOLD=9, CSR_OLD_PAR=10;
    reg [3:0] state;
    reg [15:0] va, data, par;
    reg [21:0] pa;
    reg wr, byte_op, debug_access;
    reg [5:0] entry;
    reg cache_i_valid,cache_d_valid;
    reg [5:0] cache_i_tag,cache_d_tag;
    reg [15:0] cache_i_par,cache_d_par,cache_i_pdr,cache_d_pdr;
    reg cached,cache_data;
    reg [15:0] hit_pdr;
    wire frozen=|mmr0[15:13];
    wire split=mode==0 ? mmr3[2] : mode==1 ? mmr3[1] : mode==3 ? mmr3[0] : 1'b0;
    wire [5:0] requested_entry={mode,data_space && split,virtual_address[15:13]};
    wire cache_hit=CACHE_ENABLE && (data_space ?
        cache_d_valid && cache_d_tag==requested_entry : cache_i_valid && cache_i_tag==requested_entry);
    wire [1:0] lanes=byte_op ? (va[0] ? 2'b10 : 2'b01) : 2'b11;
    wire [15:0] mask={{8{lanes[1]}},{8{lanes[0]}}};
    wire [7:0] delta={delta_amount,delta_register};
    wire apr_selected, pdr_selected;
    wire [5:0] csr_entry;
    uj11_mmu_apr_decode decode(pa,apr_selected,pdr_selected,csr_entry);
    wire [15:0] ram_data;
    wire [15:0] pdr=cached ? hit_pdr : ram_data;
    wire csr_modify=state==CSR_UPDATE && wr;
    wire mark=state==CHECK && wr && !debug_access;
    wire ram_enable=!reset && !peripheral_reset &&
        (state==PAR_READ || state==PDR_READ || state==CSR_READ || state==CSR_OLD_PAR ||
         csr_modify || state==CSR_PAR || mark);
    wire [6:0] ram_address=state==PAR_READ ? {entry,1'b0} :
        (state==PDR_READ || state==CHECK) ? {entry,1'b1} :
        {csr_entry,(state==CSR_PAR || state==CSR_OLD_PAR) ? 1'b0 : (wr || pdr_selected)};
    wire [15:0] merged=(ram_data & ~mask) | (data & mask);
    wire [15:0] ram_write_data=mark ? (pdr | 16'o100) :
        state==CSR_PAR ? merged :
        (pdr_selected ? merged : ram_data) & (pdr_selected ? 16'o177416 : 16'o177677);
    // PAR writes need their previous value for byte merge. PDR.W is first
    // cleared separately; PAR_READ uses the same synchronous port below.
    reg [15:0] old_par;
    wire [15:0] par_merged=(old_par & ~mask) | (data & mask);
    wire [1:0] ram_write=(mark || csr_modify || state==CSR_PAR) ? 2'b11 : 2'b00;
    uj11_mmu_apr_ram store(.clk(clk),.enable(ram_enable),.address(ram_address),
        .write_enable(ram_write),
        .write_data(state==CSR_PAR ? par_merged : ram_write_data),.read_data(ram_data));
    wire [21:0] translated;
    wire [2:0] abort_flags;
    uj11_mmu_translate translate(.enabled(1'b1),.map22(mmr3[4]),.writing(wr),
        .invalid_mode(entry[5:4]==2),.virtual_address(va),.par(par),.pdr(pdr),
        .physical_address(translated),.abort_flags(abort_flags),
        .ram_selected(),.io_selected(),.nxm());
    wire csr_mmr0={pa[21:1],1'b0}==22'o17777572;
    wire csr_mmr1={pa[21:1],1'b0}==22'o17777574;
    wire csr_mmr2={pa[21:1],1'b0}==22'o17777576;
    wire csr_mmr3={pa[21:1],1'b0}==22'o17772516;
    wire mmr_selected=csr_mmr0 || csr_mmr1 || csr_mmr2 || csr_mmr3;
    wire cpu_internal={pa[21:1],1'b0}==22'o17777776 ||
        {pa[21:1],1'b0}==22'o17777772 || {pa[21:1],1'b0}==22'o17777766 ||
        {pa[21:1],1'b0}==22'o17777752;
    reg opcode_fetch;
    wire [15:0] mmr_value=csr_mmr0 ? mmr0 : csr_mmr1 ? mmr1 : csr_mmr2 ? mmr2 : mmr3;
    wire [15:0] mmr_merged=(mmr_value & ~mask) | (data & mask);
    assign bus_request=state==BUS && request && !reset && !peripheral_reset;
    assign bus_write=wr;
    assign bus_byte=byte_op;
    assign bus_address=pa;
    assign bus_data=data;
    // Read both words for CSR writes so a byte PAR write preserves the other
    // lane. This preliminary read is selected before the normal CSR_PDR read.
    always @(posedge clk) begin
        if(reset || peripheral_reset) begin
            state<=IDLE;ready<=0;fault<=0;read_data<=0;
            mmr0<=0;mmr3<=0;
            // RESET clears translation controls, not the restart record.
            // Only board/power reset clears MMR1/MMR2; the next ordinary
            // instruction fetch will update them once MMR0 is unfrozen.
            if(reset)begin mmr1<=0;mmr2<=0;end
            va<=0;data<=0;pa<=0;par<=0;entry<=0;wr<=0;byte_op<=0;
            debug_access<=0;old_par<=0;
            opcode_fetch<=0;
            cache_i_valid<=0;cache_d_valid<=0;cached<=0;cache_data<=0;hit_pdr<=0;
        end else begin
            if(instruction_start && !frozen) begin mmr1<=0;mmr2<=instruction_pc;end
            if(delta_valid && !frozen) begin
                if(mmr1[7:0]==0 || instruction_start) mmr1<={8'b0,delta};
                else if(mmr1[15:8]==0) mmr1[15:8]<=delta;
            end
            case(state)
                IDLE: if(request) begin
                    va<=physical ? physical_address[15:0] : virtual_address;
                    data<=write_data;wr<=writing;
                    opcode_fetch<=instruction_start;
                    byte_op<=byte_access;debug_access<=console;
                    entry<=requested_entry;cache_data<=data_space;cached<=0;
                    fault<=0;ready<=0;
                    if(!byte_access && (physical ? physical_address[0] : virtual_address[0])) begin
                        fault<=1;ready<=1;state<=HOLD;
                    end else if(physical) begin pa<=physical_address;state<=DECODE;end
                    else if(!mmr0[0]) begin
                        pa<={ {6{&virtual_address[15:13]}},virtual_address};state<=DECODE;
                    end else if(cache_hit)begin
                        par<=data_space ? cache_d_par : cache_i_par;
                        hit_pdr<=data_space ? cache_d_pdr : cache_i_pdr;
                        cached<=1;state<=CHECK;
                    end else state<=PAR_READ;
                end
                PAR_READ: state<=PDR_READ;
                PDR_READ: begin par<=ram_data;state<=CHECK;end
                CHECK: begin
                    pa<=translated;
                    // Write-through PDR.W, including faulting writes. A stale
                    // W bit in the other I/D entry is harmless: translation
                    // ignores it, CSR reads use EBR, and marking only sets W.
                    if(CACHE_ENABLE)begin
                        if(cache_data)begin
                            cache_d_valid<=1;cache_d_tag<=entry;
                            cache_d_par<=par;cache_d_pdr<=pdr | (mark ? 16'o100 : 16'b0);
                        end else begin
                            cache_i_valid<=1;cache_i_tag<=entry;
                            cache_i_par<=par;cache_i_pdr<=pdr | (mark ? 16'o100 : 16'b0);
                        end
                    end
                    if(!debug_access && !frozen) begin
                        mmr0[6:1]<=entry;
                        if(|abort_flags) mmr0[15:13]<=abort_flags;
                    end
                    if(|abort_flags) begin fault<=3;ready<=1;state<=HOLD;end
                    else state<=DECODE;
                end
                DECODE: begin
                    // J11 internal registers are data-only. Fetching an
                    // opcode from one raises address error, not a bus timeout.
                    if(opcode_fetch && (mmr_selected || apr_selected || cpu_internal))begin
                        fault<=1;ready<=1;state<=HOLD;
                    end else if(mmr_selected) begin
                        read_data<=mmr_value;
                        if(wr) begin
                            cache_i_valid<=0;cache_d_valid<=0;
                            if(csr_mmr0) mmr0<=(mmr0 & 16'o000176) | (mmr_merged & 16'o160001);
                            if(csr_mmr3) mmr3<=mmr_merged & 16'o77;
                        end
                        ready<=1;state<=HOLD;
                    end else if(apr_selected) begin
                        // Both entries can hold the same unified I/D page.
                        // Invalidate before any PAR/PDR byte or word update.
                        if(wr)begin cache_i_valid<=0;cache_d_valid<=0;end
                        state<=wr && !pdr_selected ? CSR_OLD_PAR : CSR_READ;
                    end
                    else state<=BUS;
                end
                CSR_OLD_PAR: state<=CSR_READ;
                CSR_READ: begin
                    if(wr && !pdr_selected) old_par<=ram_data;
                    state<=CSR_UPDATE;
                end
                CSR_UPDATE: begin
                    if(wr && !pdr_selected) begin
                        state<=CSR_PAR;
                    end else begin read_data<=ram_data;ready<=1;state<=HOLD;end
                end
                CSR_PAR: begin ready<=1;state<=HOLD;end
                BUS: if(bus_ready) begin
                    read_data<=bus_read_data;fault<=bus_error ? 2 : 0;ready<=1;state<=HOLD;
                end
                HOLD: if(!request) begin ready<=0;state<=IDLE;end
                default: state<=IDLE;
            endcase
        end
    end
endmodule
