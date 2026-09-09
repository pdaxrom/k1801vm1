`timescale 1ns/1ps
// Microcoded execution engine. Dispatch is an external, separately measured
// combinational function of dispatch_ir (incoming data during FETCH).
module uj11_engine (
    input wire clk, reset,
    input wire irq_valid,
    input wire [2:0] irq_priority,
    input wire [8:1] irq_vector,
    output wire irq_ack, waiting,
    output reg peripheral_reset,
    input wire [9:0] dispatch_address,
    output wire [15:0] dispatch_ir,
    output wire [15:0] mem_addr, mem_write_data,
    output wire mem_request, mem_read, mem_write, mem_byte,
    input wire mem_ack, mem_error,
    input wire [15:0] mem_read_data,
    output wire stopped,
    output wire [1:0] fault_code,
    output reg retire,
    output wire [9:0] debug_upc,
    output wire [35:0] debug_uword,
    output reg [15:0] ir, mdr,
    output wire [15:0] psw, q,
    output wire debug_rf_write,
    output wire [3:0] debug_rf_address,
    output wire [15:0] debug_rf_data
);
    wire [35:0] uword;
    wire [9:0] next_address;
    wire control = uword[35];
    wire [3:0] command = uword[34:31];
    wire fetching = control && command==4'd2;
    wire reading = control && command==4'd11;
    wire writing = control && command==4'd12;
    wire stopping = control && command==4'd13;
    wire wait_command = control && command==4'd14;
    reg wait_seen, frame_active, fault_repair, trace_latched;
    // 0 instruction, 1 IRQ frame, 2 memory-fault frame, 3 trace frame.
    reg [1:0] irq_active;
    wire trap_command = control && command==4'd15;
    wire irq_pending = irq_valid && irq_priority > psw[7:5] && !irq_active[1];
    wire alu_boundary = !control &&
                        (uword[9:8]==2'd2 || (uword[9:8]==2'd3 && read_a==16'd1));
    // A resolved vector is held by the source through the accepting clock.
    // IRQ completes the previous instruction, then reuses MDR for its vector.
    // T is sampled before an instruction; RTI instead uses restored T and
    // RTT suppresses its own trace. WAIT checks T only after its first step.
    wire return_trace = uword[0] && uword[12:10]!=3'd6;
    wire trace_pending = irq_active==0 && (wait_command ? (wait_seen && psw[4]) :
                         (return_trace ? (psw[4] && !ir[2]) : trace_latched));
    wire trace_ack = step && trace_pending && (alu_boundary || wait_command);
    assign irq_ack = step && irq_pending && !trace_pending && (alu_boundary || wait_command);
    assign waiting = wait_command && !reset && !stopped;
    // Register JUMP.init for peripherals with asynchronous reset inputs.
    // Pulse covers the following settling word; IRQ samples after release.
    always @(posedge clk) begin
        if (reset) peripheral_reset <= 0;
        else peripheral_reset <= running && control && command==4'd0 && uword[0];
    end

    wire memory_op = fetching || reading || writing;
    wire [4:0] a_select = uword[30:26];
    wire [4:0] b_select = uword[25:21];
    function [3:0] select_register;
        input [4:0] selector;
        input [5:0] registers;
        begin
            if (selector[4])
                select_register = {1'b0, selector[0] ? registers[2:0] : registers[5:3]} |
                                  {3'b0,selector[1]};
            else select_register = selector[3:0];
        end
    endfunction
    wire [3:0] a = select_register(a_select,{ir[8:6],ir[2:0]});
    wire [3:0] b = select_register(b_select,{ir[8:6],ir[2:0]});
    wire [15:0] read_a, read_b, result;
    wire [3:0] nzvc;
    reg [15:0] d;
    reg [1:0] fault_latched;
    wire [1:0] bus_fault;
    wire complete;
    wire running = !reset && !stopped;
    wire ready = !memory_op || complete;
    wire step = running && ready && bus_fault==0;
    // Faults redirect the microstore without committing the failed operation.
    // A second fault during vector/frame construction remains terminal.
    wire fault_redirect = running && bus_fault!=0 && !frame_active;
    wire advance = running && ready && !(frame_active && bus_fault!=0);
    // v3 FETCH encodes ADD/AD/RF/TWO directly. Other control words do not
    // commit datapath results, so their overlapping ALU fields are don't-care.
    wire [3:0] operation = uword[34:31];
    wire [2:0] pair = uword[20:18];
    wire [2:0] destination = (control && !fetching) ? 3'd0 : uword[17:15];
    wire [1:0] flags = control ? 2'd0 : uword[14:13];
    // Among accepted high-bit opcodes, SUB and branches remain word-sized.
    // Unsupported opcodes enter a trap; recheck this qualifier when
    // enabling new high-bit system/EIS classes.
    wire byte_instruction = ir[15] && ir[14:12]!=3'd6 && (|ir[14:11]);
    // EA and ordinary RF temporaries are always word. Operand writes and
    // NZV/NZVC flag commits select byte arithmetic for a byte instruction.
    wire byte_operation = !control && byte_instruction &&
                          (flags==2'd1 || flags==2'd2 || destination==3'd5 || destination==3'd6);
    always @* begin
        case (uword[12:10])
            3'd0: d = 0;
            3'd1: d = 1;
            3'd2: d = 2;
            3'd3: d = (byte_instruction && a<4'd6) ? 16'd1 : 16'd2;
            3'd4: d = mdr;
            // DISP is signed branch byte or unsigned SOB six-bit offset.
            // MARK also selects DISP; its IR[7:6]=0 gives an unsigned six-bit offset.
            3'd5: d = {{7{ir[7] && !ir[14]}},(ir[7:6] & {2{!ir[14]}}),ir[5:0],1'b0};
            3'd6: d = {8'b0,uword[7:0]};
            3'd7: d = psw;
        endcase
    end
    uj11_datapath dp(.clk(clk),.reset(reset),.enable(step),.a(a),.b(b),
        .operation(operation),.pair(pair),.destination(destination),.d(d),
        .carry(psw[0]),.byte_mode(byte_operation),.read_a(read_a),.read_b(read_b),.result(result),.q(q),
        .nzvc(nzvc),.rf_write(debug_rf_write),.writeback(debug_rf_data));
    uj11_psw status(.clk(clk),.reset(reset),.enable(step),.update(flags),
        .nzvc(nzvc),.value(result),.psw(psw));
    uj11_mem memory(.active(memory_op && !reset && !stopped),
        .writing(writing),.byte_access(control && (uword[6] || (uword[3] && byte_instruction)) && !fetching),
        .address(read_a),.data(read_b),.ack(mem_ack),.error(mem_error),
        .request(mem_request),.read(mem_read),.write(mem_write),.byte_word(mem_byte),
        .addr(mem_addr),.write_data(mem_write_data),.complete(complete),.fault(bus_fault));
    uj11_microseq seq(.clk(clk),.reset(reset),.enable(advance),.uword(uword),.ir(ir),
        .nzvc(psw[3:0]),.dispatch_address(dispatch_address),.address_odd(read_a[0]),
        .byte_instruction(byte_instruction),.selected_a(a),.q0(q[0]),
        .loop_zero(1'b0),.bus_error(bus_fault!=0),.a_one(read_a==16'd1),.irq_pending(irq_pending),.trace_pending(trace_pending),.fault_redirect(fault_redirect),.fault_repair(fault_repair),.upc(debug_upc),.next_address(next_address));
`ifdef UJ11_VENDOR_ROM
    uj11_rom rom(.clk(clk),.enable(reset || advance),.address(next_address),.data(uword));
`else
    uj11_rom #(.IMAGE("microcode/generated/m0.mem")) rom(
        .clk(clk),.enable(reset || advance),.address(next_address),.data(uword));
`endif
    assign dispatch_ir = fetching ? mem_read_data : ir;
    assign stopped = (fault_latched!=0) || stopping;
    assign fault_code = fault_latched!=0 ? fault_latched : stopping ? 2'd3 : 2'd0;
    assign debug_uword = uword;
    assign debug_rf_address = b;
    always @(posedge clk) begin
        if (reset) begin
            ir <= 0;
            mdr <= 0;
            fault_latched <= 0;
            retire <= 0;
            wait_seen <= 0;
            irq_active <= 0;
            frame_active <= 0;
            fault_repair <= 0;
            trace_latched <= 0;
        end else begin
            retire <= step && ((alu_boundary && irq_active==0) || (wait_command && !wait_seen));
            if (step) wait_seen <= wait_command;
            // These READ continuations contain exactly one architectural +1/+2.
            // Defer only that delta until after a failed bus edge; preserve the
            // successful stream-read/PC-update order and its prefetch hit.
            if (fault_redirect) fault_repair <= reading && uword[2];
            else if (step) fault_repair <= 0;
            if (fault_redirect) irq_active <= 2'd2;
            else if (trace_ack) irq_active <= 2'd3;
            else if (irq_ack) irq_active <= 2'd1;
            else if (step && alu_boundary) irq_active <= 0;
            if (irq_ack) mdr <= {7'b0,irq_vector,1'b0};
            if (bus_fault!=0 && frame_active) fault_latched <= bus_fault;
            if (step && trap_command) frame_active <= 1;
            else if (step && alu_boundary) frame_active <= 0;
            if (step && (fetching || reading)) mdr <= mem_read_data;
            if (step && fetching) begin
                ir <= mem_read_data;
                trace_latched <= psw[4];
            end
        end
    end
endmodule
